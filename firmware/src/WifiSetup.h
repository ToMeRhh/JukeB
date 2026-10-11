#pragma once

#include <Arduino.h>
#include <DNSServer.h>

#include <cstdint>
#include <string>

#include "RetrySchedule.h"

// Joins the configured network; if that is impossible (no SSID, or no link for
// 20 s at boot / 60 s later) it opens the "Jukebox-Setup" access point with a
// captive DNS so any site redirects to the setup page. While the AP is up and an
// SSID is configured, it keeps retrying the home network every 30 s and switches
// back to station mode as soon as it connects (routers often come back minutes
// after a power cut).
class WifiSetup {
 public:
  enum class Mode { Sta, Ap };
  static constexpr const char* kApSsid = "Jukebox-Setup";
  static constexpr uint32_t kBootConnectMs = 20000;
  static constexpr uint32_t kLinkLossToApMs = 60000;
  static constexpr uint32_t kRetryIntervalMs = 30000;

  void begin(const std::string& ssid, const std::string& pass, const std::string& hostname);
  void loop(uint32_t now);

  Mode mode() const { return mode_; }
  bool connected() const;
  String ip() const;
  int rssi() const;

 private:
  void startAp();
  void onConnected();

  DNSServer dns_;
  Mode mode_ = Mode::Ap;
  std::string ssid_, pass_, hostname_;
  uint32_t disconnectedSince_ = 0;
  RetrySchedule retry_{kRetryIntervalMs};
};
