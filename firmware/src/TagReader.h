#pragma once

#include <MFRC522DriverPinSimple.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522v2.h>

#include <cstdint>
#include <string>

#include "PresenceDebouncer.h"

// Polls the RC522 every 100 ms and returns debounced presence events.
class TagReader {
 public:
  static constexpr uint32_t kPollMs = 100;

  TagReader(uint8_t ssPin, uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin);

  // SPI + chip init. False when the version register reads 0x00/0xFF.
  bool begin();
  // Call every loop(); returns at most one event.
  PresenceEvent poll(uint32_t nowMs);

  bool found() const { return found_; }
  uint8_t version() const { return version_; }
  bool present() const { return debouncer_.present(); }

  static std::string formatUid(const uint8_t* bytes, uint8_t len);

 private:
  bool readUid(std::string& out);

  uint8_t ssPinNum_, sckPin_, misoPin_, mosiPin_;
  MFRC522DriverPinSimple ssPin_;
  MFRC522DriverSPI driver_;
  MFRC522 mfrc_;
  PresenceDebouncer debouncer_;
  uint32_t lastPoll_ = 0;
  bool found_ = false;
  uint8_t version_ = 0;
};
