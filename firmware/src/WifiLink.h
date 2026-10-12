#pragma once

#include <cstdint>

// Station-only WiFi that keeps retrying forever.
class WifiLink {
 public:
  static constexpr uint32_t kRetryMs = 10000;

  void begin(const char* ssid, const char* password, const char* hostname);
  void loop(uint32_t nowMs);
  bool connected() const;
  int rssi() const;

 private:
  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
  uint32_t lastAttempt_ = 0;
  bool wasConnected_ = false;
};
