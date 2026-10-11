#include "TagReader.h"

#include <Arduino.h>
#include <SPI.h>

#include <cstdio>

TagReader::TagReader(uint8_t ssPin) : ssPin_(ssPin), driver_(ssPin_), mfrc_(driver_) {}

bool TagReader::begin() {
  SPI.begin();  // VSPI defaults: SCK 18, MISO 19, MOSI 23
  mfrc_.PCD_Init();
  delay(4);  // some boards need a moment after init before the version register is valid
  version_ = static_cast<uint8_t>(mfrc_.PCD_GetVersion());
  found_ = version_ != 0x00 && version_ != 0xFF;
  Serial.printf("[reader] RC522 version 0x%02X %s\n", version_, found_ ? "ok" : "NOT FOUND");
  return found_;
}

std::string TagReader::formatUid(const uint8_t* bytes, uint8_t len) {
  std::string out;
  char buf[4];
  for (uint8_t i = 0; i < len; ++i) {
    std::snprintf(buf, sizeof(buf), "%02X", bytes[i]);
    if (i) out.push_back(':');
    out += buf;
  }
  return out;
}

bool TagReader::readUid(std::string& out) {
  // Reset baud rates and modulation width, as the library's PICC_IsNewCardPresent does.
  driver_.PCD_WriteRegister(MFRC522Constants::PCD_Register::TxModeReg, 0x00);
  driver_.PCD_WriteRegister(MFRC522Constants::PCD_Register::RxModeReg, 0x00);
  driver_.PCD_WriteRegister(MFRC522Constants::PCD_Register::ModWidthReg, 0x26);

  // WUPA wakes both idle and halted cards, so a card that stays on the reader
  // answers every poll even after we halt it.
  uint8_t atqa[2];
  uint8_t atqaSize = sizeof(atqa);
  const MFRC522Constants::StatusCode st = mfrc_.PICC_WakeupA(atqa, &atqaSize);
  if (st != MFRC522Constants::StatusCode::STATUS_OK && st != MFRC522Constants::StatusCode::STATUS_COLLISION) {
    return false;
  }
  if (!mfrc_.PICC_ReadCardSerial()) return false;
  out = formatUid(mfrc_.uid.uidByte, mfrc_.uid.size);
  mfrc_.PICC_HaltA();
  return true;
}

void TagReader::poll(uint32_t nowMs) {
  if (!found_) return;
  if (nowMs - lastPoll_ < kPollMs) return;
  lastPoll_ = nowMs;

  std::string uid;
  const bool seen = readUid(uid);
  const PresenceTracker::Result r = tracker_.update(seen, uid, nowMs);
  if (r.removed) {
    Serial.printf("[reader] removed %s\n", r.removedUid.c_str());
    if (removed_) removed_(r.removedUid);
  }
  if (r.placed) {
    Serial.printf("[reader] placed %s\n", r.placedUid.c_str());
    if (placed_) placed_(r.placedUid);
  }
}
