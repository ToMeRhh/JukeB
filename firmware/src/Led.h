#pragma once

#include <cstdint>

// Non-blocking onboard LED: short blink patterns on top of a slow "WiFi down" blink.
class Led {
 public:
  explicit Led(uint8_t pin) : pin_(pin) {}
  void begin();
  void blink(uint8_t count, uint16_t onMs, uint16_t offMs);
  void setSlowBlink(bool enabled);
  void loop(uint32_t nowMs);

 private:
  void write(bool on);

  uint8_t pin_;
  uint8_t remaining_ = 0;  // half-phases left in the current pattern
  bool phaseOn_ = false;
  uint16_t onMs_ = 0;
  uint16_t offMs_ = 0;
  uint32_t phaseStart_ = 0;
  bool slow_ = false;
  bool slowState_ = false;
  uint32_t slowToggle_ = 0;
};
