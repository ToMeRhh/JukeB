#include "HaClient.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

namespace {

HaClient::Response postOnce(const std::string& url, const std::string& json) {
  HaClient::Response r;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (!http.begin(client, url.c_str())) {
    r.httpCode = -1;
    r.body = "invalid url";
    return r;
  }
  http.addHeader("Content-Type", "application/json");
  r.httpCode = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(json.data())), json.size());
  if (r.httpCode > 0) {
    r.body = http.getString().c_str();
  } else {
    r.body = HTTPClient::errorToString(r.httpCode).c_str();
  }
  http.end();
  return r;
}

}  // namespace

namespace HaClient {

Response post(const std::string& url, const std::string& json) {
  Response r = postOnce(url, json);
  if (r.httpCode <= 0) {
    delay(300);
    r = postOnce(url, json);
  }
  return r;
}

}  // namespace HaClient
