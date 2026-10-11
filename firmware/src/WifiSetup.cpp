#include "WifiSetup.h"

#include <ESPmDNS.h>
#include <WiFi.h>

void WifiSetup::begin(const std::string& ssid, const std::string& pass, const std::string& hostname) {
  ssid_ = ssid;
  pass_ = pass;
  hostname_ = hostname;
  if (ssid_.empty()) {
    Serial.println("[wifi] no SSID configured, starting setup AP");
    startAp();
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname_.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid_.c_str(), pass_.c_str());
  Serial.printf("[wifi] connecting to %s", ssid_.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kBootConnectMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] connection failed, starting setup AP (will keep retrying)");
    startAp();
    return;
  }
  onConnected();
}

void WifiSetup::onConnected() {
  if (mode_ == Mode::Ap) {
    dns_.stop();
    WiFi.mode(WIFI_STA);  // drop the setup AP, keep the station link
  }
  mode_ = Mode::Sta;
  disconnectedSince_ = 0;
  retry_.reset();
  Serial.printf("[wifi] connected, ip %s\n", WiFi.localIP().toString().c_str());
  MDNS.end();
  if (MDNS.begin(hostname_.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[wifi] mDNS %s.local\n", hostname_.c_str());
  }
}

void WifiSetup::startAp() {
  // With an SSID configured keep the station side alive so we can retry it.
  WiFi.mode(ssid_.empty() ? WIFI_AP : WIFI_AP_STA);
  WiFi.softAP(kApSsid);
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(53, "*", WiFi.softAPIP());
  mode_ = Mode::Ap;
  retry_.reset();
  Serial.printf("[wifi] AP %s up, ip %s\n", kApSsid, WiFi.softAPIP().toString().c_str());
}

void WifiSetup::loop(uint32_t now) {
  if (mode_ == Mode::Ap) {
    dns_.processNextRequest();
    if (ssid_.empty()) return;
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[wifi] home network is back, leaving setup mode");
      onConnected();
      return;
    }
    if (retry_.due(now)) {
      retry_.mark(now);
      Serial.printf("[wifi] retrying %s\n", ssid_.c_str());
      WiFi.begin(ssid_.c_str(), pass_.c_str());
    }
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSince_ = 0;
    return;
  }
  if (disconnectedSince_ == 0) {
    disconnectedSince_ = now;
    Serial.println("[wifi] link lost, waiting for reconnect");
  } else if (now - disconnectedSince_ > kLinkLossToApMs) {
    Serial.println("[wifi] no link for 60 s, falling back to setup AP");
    startAp();
  }
}

bool WifiSetup::connected() const {
  return mode_ == Mode::Sta && WiFi.status() == WL_CONNECTED;
}

String WifiSetup::ip() const {
  return mode_ == Mode::Ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

int WifiSetup::rssi() const {
  return mode_ == Mode::Sta ? WiFi.RSSI() : 0;
}
