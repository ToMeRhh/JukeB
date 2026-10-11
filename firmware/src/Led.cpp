#include "Led.h"

#include <Arduino.h>

void Led::begin() {
  pinMode(pin_, OUTPUT);
  write(base_);
}

void Led::write(bool on) {
  digitalWrite(pin_, on ? HIGH : LOW);
}

void Led::setBase(bool on) {
  base_ = on;
  if (remaining_ == 0) write(base_);
}

void Led::blink(uint8_t count, uint16_t onMs, uint16_t offMs) {
  remaining_ = static_cast<uint8_t>(count * 2);
  onMs_ = onMs;
  offMs_ = offMs;
  phaseOn_ = true;
  phaseStart_ = millis();
  write(!base_);  // first phase is "inverted base" so a blink is visible in both base states
}

void Led::loop(uint32_t now) {
  if (remaining_ == 0) return;
  const uint16_t phaseLen = phaseOn_ ? onMs_ : offMs_;
  if (now - phaseStart_ < phaseLen) return;
  --remaining_;
  phaseStart_ = now;
  if (remaining_ == 0) {
    write(base_);
    return;
  }
  phaseOn_ = !phaseOn_;
  write(phaseOn_ ? !base_ : base_);
}
