#include "WifiLink.h"

#include <Arduino.h>
#include <WiFi.h>

void WifiLink::begin(const char* ssid, const char* password, const char* hostname) {
  ssid_ = ssid;
  password_ = password;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid_, password_);
  lastAttempt_ = millis();
  Serial.printf("[wifi] connecting to %s\n", ssid_);
}

void WifiLink::loop(uint32_t nowMs) {
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up != wasConnected_) {
    wasConnected_ = up;
    if (up) {
      Serial.printf("[wifi] connected, ip %s, rssi %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      Serial.println("[wifi] disconnected");
    }
  }
  if (!up && nowMs - lastAttempt_ >= kRetryMs) {
    lastAttempt_ = nowMs;
    Serial.println("[wifi] retrying");
    WiFi.disconnect();
    WiFi.begin(ssid_, password_);
  }
}

bool WifiLink::connected() const { return WiFi.status() == WL_CONNECTED; }

int WifiLink::rssi() const { return connected() ? WiFi.RSSI() : 0; }
