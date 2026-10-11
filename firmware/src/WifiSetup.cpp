#include "WifiSetup.h"

#include <ESPmDNS.h>
#include <WiFi.h>

void WifiSetup::begin(const std::string& ssid, const std::string& pass, const std::string& hostname) {
  hostname_ = hostname;
  if (ssid.empty()) {
    Serial.println("[wifi] no SSID configured, starting setup AP");
    startAp();
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname_.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("[wifi] connecting to %s", ssid.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kBootConnectMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] connection failed, starting setup AP");
    startAp();
    return;
  }

  mode_ = Mode::Sta;
  disconnectedSince_ = 0;
  Serial.printf("[wifi] connected, ip %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin(hostname_.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[wifi] mDNS %s.local\n", hostname_.c_str());
  }
}

void WifiSetup::startAp() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(kApSsid);
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(53, "*", WiFi.softAPIP());
  mode_ = Mode::Ap;
  Serial.printf("[wifi] AP %s up, ip %s\n", kApSsid, WiFi.softAPIP().toString().c_str());
}

void WifiSetup::loop(uint32_t now) {
  if (mode_ == Mode::Ap) {
    dns_.processNextRequest();
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
