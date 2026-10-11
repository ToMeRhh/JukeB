#include "HaClient.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

namespace {

int postOnce(const std::string& url, const std::string& payload, std::string& message) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(client, url.c_str())) {
    message = "invalid url";
    return -1;
  }
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(payload.data())), payload.size());
  if (code < 0) {
    message = HTTPClient::errorToString(code).c_str();
  } else {
    message = "HTTP " + std::to_string(code);
  }
  http.end();
  return code;
}

}  // namespace

namespace HaClient {

HaResult send(const std::string& url, const std::string& payload) {
  HaResult r;
  r.any = true;
  if (WiFi.status() != WL_CONNECTED) {
    r.ok = false;
    r.httpCode = 0;
    r.message = "wifi not connected";
    r.atMillis = millis();
    Serial.printf("[ha] skipped: %s\n", r.message.c_str());
    return r;
  }
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (attempt > 0) delay(500);
    std::string message;
    const int code = postOnce(url, payload, message);
    r.httpCode = code;
    r.message = message;
    r.ok = code >= 200 && code < 300;
    r.atMillis = millis();
    Serial.printf("[ha] POST %s attempt %d -> %s\n", url.c_str(), attempt + 1, message.c_str());
    if (r.ok) break;
  }
  return r;
}

}  // namespace HaClient
