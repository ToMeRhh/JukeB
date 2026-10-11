#include "TagRegistry.h"

#include <cctype>
#include <utility>

static void setErr(std::string* err, const std::string& msg) {
  if (err) *err = msg;
}

std::string TagRegistry::normalizeUid(const std::string& uid) {
  std::string out;
  for (char c : uid) {
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

bool TagRegistry::validate(const Tag& t, std::string* err) {
  if (t.name.empty()) { setErr(err, "name is required"); return false; }
  if (t.uri.empty()) { setErr(err, "uri is required"); return false; }
  if (t.contentType.empty()) { setErr(err, "content_type is required"); return false; }
  const bool volumeOk = t.volume == kVolumeUseDefault || t.volume == kVolumeNoChange ||
                        (t.volume >= 0.0f && t.volume <= 1.0f);
  if (!volumeOk) { setErr(err, "volume must be between 0 and 1, or -1"); return false; }
  return true;
}

bool TagRegistry::tagFromJson(JsonVariantConst v, Tag& out, std::string* err) {
  if (!v.is<JsonObjectConst>()) { setErr(err, "tag must be an object"); return false; }
  Tag t;
  t.name = v["name"] | "";
  t.uri = v["uri"] | "";
  t.contentType = v["content_type"] | "music";
  if (t.contentType.empty()) t.contentType = "music";
  t.player = v["player"] | "";
  JsonVariantConst vol = v["volume"];
  if (vol.isNull()) {
    t.volume = kVolumeUseDefault;
  } else if (vol.is<float>()) {
    t.volume = vol.as<float>();
  } else {
    setErr(err, "volume must be a number");
    return false;
  }
  t.shuffle = v["shuffle"] | false;
  if (!validate(t, err)) return false;
  out = t;
  return true;
}

void TagRegistry::tagToJson(const Tag& t, JsonObject o) {
  o["name"] = t.name;
  o["uri"] = t.uri;
  o["content_type"] = t.contentType;
  o["player"] = t.player;
  if (t.volume != kVolumeUseDefault) o["volume"] = t.volume;
  o["shuffle"] = t.shuffle;
}

bool TagRegistry::loadJson(const std::string& json, std::string* err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { setErr(err, std::string("parse error: ") + e.c_str()); return false; }
  if (!doc.is<JsonObjectConst>()) { setErr(err, "root must be an object"); return false; }
  const int version = doc["version"] | 0;
  if (version != kVersion) { setErr(err, "unsupported version"); return false; }

  std::map<std::string, Tag> loaded;
  for (JsonPairConst kv : doc["tags"].as<JsonObjectConst>()) {
    Tag t;
    std::string terr;
    if (!tagFromJson(kv.value(), t, &terr)) {
      setErr(err, std::string("tag ") + kv.key().c_str() + ": " + terr);
      return false;
    }
    const std::string uid = normalizeUid(kv.key().c_str());
    if (uid.empty()) { setErr(err, "empty uid"); return false; }
    if (loaded.size() >= kMaxTags) { setErr(err, "too many tags"); return false; }
    loaded[uid] = t;
  }
  tags_ = std::move(loaded);
  return true;
}

std::string TagRegistry::toJson() const {
  JsonDocument doc;
  doc["version"] = kVersion;
  JsonObject tags = doc["tags"].to<JsonObject>();
  for (const auto& kv : tags_) {
    JsonObject o = tags[kv.first].to<JsonObject>();
    tagToJson(kv.second, o);
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool TagRegistry::find(const std::string& uid, Tag& out) const {
  auto it = tags_.find(normalizeUid(uid));
  if (it == tags_.end()) return false;
  out = it->second;
  return true;
}

TagRegistry::UpsertResult TagRegistry::upsert(const std::string& rawUid, const Tag& tag, std::string* err) {
  const std::string uid = normalizeUid(rawUid);
  if (uid.empty()) { setErr(err, "uid is required"); return UpsertResult::Invalid; }
  if (!validate(tag, err)) return UpsertResult::Invalid;
  if (tags_.find(uid) == tags_.end() && tags_.size() >= kMaxTags) {
    setErr(err, "registry is full");
    return UpsertResult::Full;
  }
  tags_[uid] = tag;
  return UpsertResult::Ok;
}

bool TagRegistry::remove(const std::string& rawUid) {
  return tags_.erase(normalizeUid(rawUid)) > 0;
}
