#pragma once

#include <MFRC522DriverPinSimple.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522v2.h>

#include <cstdint>
#include <functional>
#include <string>

#include "PresenceTracker.h"

// Polls the RC522 every 100 ms and reports tag placed / removed events.
// Everything runs on the caller's thread (loop()); callbacks fire from poll().
class TagReader {
 public:
  using Callback = std::function<void(const std::string& uid)>;
  static constexpr uint32_t kPollMs = 100;

  explicit TagReader(uint8_t ssPin);

  // Initialises SPI and the chip. Returns false if the chip does not answer
  // (version register reads 0x00 or 0xFF).
  bool begin();
  void setGraceMs(uint32_t ms) { tracker_.setGraceMs(ms); }
  void onPlaced(Callback cb) { placed_ = std::move(cb); }
  void onRemoved(Callback cb) { removed_ = std::move(cb); }
  void poll(uint32_t nowMs);

  bool found() const { return found_; }
  uint8_t version() const { return version_; }
  bool present() const { return tracker_.present(); }

  static std::string formatUid(const uint8_t* bytes, uint8_t len);

 private:
  bool readUid(std::string& out);

  MFRC522DriverPinSimple ssPin_;
  MFRC522DriverSPI driver_;
  MFRC522 mfrc_;
  PresenceTracker tracker_;
  Callback placed_;
  Callback removed_;
  uint32_t lastPoll_ = 0;
  bool found_ = false;
  uint8_t version_ = 0;
};
