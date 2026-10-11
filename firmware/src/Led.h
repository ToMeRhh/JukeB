#pragma once

#include <cstdint>

// Non-blocking status LED: a base state (on/off) plus short blink bursts on top.
class Led {
 public:
  explicit Led(uint8_t pin) : pin_(pin) {}
  void begin();
  void setBase(bool on);
  void blink(uint8_t count, uint16_t onMs = 80, uint16_t offMs = 120);
  void loop(uint32_t now);

 private:
  void write(bool on);
  uint8_t pin_;
  bool base_ = false;
  uint8_t remaining_ = 0;  // blink phases left (on+off = 2 per blink)
  bool phaseOn_ = false;
  uint16_t onMs_ = 80, offMs_ = 120;
  uint32_t phaseStart_ = 0;
};
