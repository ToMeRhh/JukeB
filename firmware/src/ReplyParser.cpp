#include "ReplyParser.h"

#include <ArduinoJson.h>

namespace ReplyParser {

Reply parse(int httpCode, const std::string& body) {
  Reply r;
  if (httpCode <= 0) {
    r.message = "connection failed";
    return r;
  }
  JsonDocument doc;
  const bool isJson = deserializeJson(doc, body) == DeserializationError::Ok && doc.is<JsonObjectConst>();

  if (httpCode < 200 || httpCode >= 300) {
    r.message = "HTTP " + std::to_string(httpCode);
    if (isJson && doc["error"].is<const char*>()) {
      r.message += ": ";
      r.message += doc["error"].as<const char*>();
    }
    return r;
  }
  if (!isJson || !doc["ok"].is<bool>()) {
    r.message = "HTTP " + std::to_string(httpCode) +
                " but no JukeB reply (is the integration installed and the webhook id right?)";
    return r;
  }
  r.ok = doc["ok"].as<bool>();
  if (!r.ok) {
    r.message = "rejected";
    if (doc["error"].is<const char*>()) r.message += std::string(": ") + doc["error"].as<const char*>();
    return r;
  }
  if (doc["known"].is<bool>()) {
    r.known = doc["known"].as<bool>();
    r.message = r.known ? "ok, known" : "ok, unknown tag";
  } else {
    r.message = "ok";
  }
  return r;
}

}  // namespace ReplyParser
