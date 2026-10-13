#include "TagReader.h"

#include <Arduino.h>
#include <SPI.h>

#include <cstdio>

TagReader::TagReader(uint8_t ssPin, uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin)
    : ssPinNum_(ssPin), sckPin_(sckPin), misoPin_(misoPin), mosiPin_(mosiPin),
      ssPin_(ssPin), driver_(ssPin_), mfrc_(driver_) {}

bool TagReader::begin() {
  SPI.begin(sckPin_, misoPin_, mosiPin_, ssPinNum_);  // any pins work via the GPIO matrix at RC522 speeds
  mfrc_.PCD_Init();
  delay(4);
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

  // WUPA wakes idle and halted cards, so a card left on the reader answers every poll.
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

PresenceEvent TagReader::poll(uint32_t nowMs) {
  PresenceEvent none;
  if (!found_) return none;
  if (nowMs - lastPoll_ < kPollMs) return none;
  lastPoll_ = nowMs;

  std::string uid;
  const bool seen = readUid(uid);
  return debouncer_.update(seen, uid);
}
