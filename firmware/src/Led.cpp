#include "Led.h"

#include <Arduino.h>

void Led::begin() {
  pinMode(pin_, OUTPUT);
  write(false);
}

void Led::write(bool on) { digitalWrite(pin_, on ? HIGH : LOW); }

void Led::blink(uint8_t count, uint16_t onMs, uint16_t offMs) {
  remaining_ = static_cast<uint8_t>(count * 2);
  onMs_ = onMs;
  offMs_ = offMs;
  phaseOn_ = true;
  phaseStart_ = millis();
  write(true);
}

void Led::setSlowBlink(bool enabled) {
  if (slow_ == enabled) return;
  slow_ = enabled;
  if (!slow_ && remaining_ == 0) write(false);
}

void Led::loop(uint32_t nowMs) {
  if (remaining_ > 0) {
    const uint16_t phaseLen = phaseOn_ ? onMs_ : offMs_;
    if (nowMs - phaseStart_ < phaseLen) return;
    --remaining_;
    phaseStart_ = nowMs;
    if (remaining_ == 0) {
      write(slow_ ? slowState_ : false);
      return;
    }
    phaseOn_ = !phaseOn_;
    write(phaseOn_);
    return;
  }
  if (slow_ && nowMs - slowToggle_ >= 500) {
    slowToggle_ = nowMs;
    slowState_ = !slowState_;
    write(slowState_);
  }
}
