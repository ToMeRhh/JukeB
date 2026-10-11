#include "HaPayload.h"

#include <ArduinoJson.h>

namespace HaPayload {

std::string effectivePlayer(const Tag& tag, const Settings& s) {
  return tag.player.empty() ? s.defaultPlayer : tag.player;
}

float effectiveVolume(const Tag& tag, const Settings& s) {
  if (tag.volume == kVolumeUseDefault) return s.defaultVolume;
  return tag.volume;  // 0..1 or kVolumeNoChange
}

bool hasPlayer(const Tag& tag, const Settings& s) {
  return !effectivePlayer(tag, s).empty();
}

static std::string serialize(const JsonDocument& doc) {
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string play(const std::string& uid, const Tag& tag, const Settings& s) {
  JsonDocument doc;
  doc["event"] = "play";
  doc["uid"] = uid;
  doc["name"] = tag.name;
  doc["uri"] = tag.uri;
  doc["content_type"] = tag.contentType;
  doc["player"] = effectivePlayer(tag, s);
  const float volume = effectiveVolume(tag, s);
  if (volume != kVolumeNoChange) doc["volume"] = volume;
  doc["shuffle"] = tag.shuffle;
  return serialize(doc);
}

std::string control(const std::string& event, const std::string& uid, const Tag& tag, const Settings& s) {
  JsonDocument doc;
  doc["event"] = event;
  doc["uid"] = uid;
  doc["player"] = effectivePlayer(tag, s);
  return serialize(doc);
}

std::string test() {
  JsonDocument doc;
  doc["event"] = "test";
  return serialize(doc);
}

}  // namespace HaPayload
