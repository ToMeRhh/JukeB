#pragma once

#include <Arduino.h>
#include <DNSServer.h>

#include <cstdint>
#include <string>

// Joins the configured network; if that is impossible (no SSID, or no link for
// 20 s at boot / 60 s later) it opens the "Jukebox-Setup" access point with a
// captive DNS so any site redirects to the setup page.
class WifiSetup {
 public:
  enum class Mode { Sta, Ap };
  static constexpr const char* kApSsid = "Jukebox-Setup";
  static constexpr uint32_t kBootConnectMs = 20000;
  static constexpr uint32_t kLinkLossToApMs = 60000;

  void begin(const std::string& ssid, const std::string& pass, const std::string& hostname);
  void loop(uint32_t now);

  Mode mode() const { return mode_; }
  bool connected() const;
  String ip() const;
  int rssi() const;

 private:
  void startAp();

  DNSServer dns_;
  Mode mode_ = Mode::Ap;
  std::string hostname_;
  uint32_t disconnectedSince_ = 0;
};
