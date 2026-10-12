#include "EventPayload.h"

#include <ArduinoJson.h>

namespace {

JsonDocument base(const EventPayload::BoxInfo& box, const char* event) {
  JsonDocument doc;
  doc["event"] = event;
  doc["box_id"] = box.boxId;
  doc["version"] = box.version;
  return doc;
}

std::string dump(const JsonDocument& doc) {
  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace

namespace EventPayload {

std::string present(const BoxInfo& box, const std::string& uid) {
  JsonDocument doc = base(box, "present");
  doc["uid"] = uid;
  return dump(doc);
}

std::string absent(const BoxInfo& box, const std::string& uid) {
  JsonDocument doc = base(box, "absent");
  doc["uid"] = uid;
  return dump(doc);
}

std::string heartbeat(const BoxInfo& box, uint32_t uptimeS, int rssi) {
  JsonDocument doc = base(box, "heartbeat");
  doc["uptime_s"] = uptimeS;
  doc["rssi"] = rssi;
  return dump(doc);
}

}  // namespace EventPayload
