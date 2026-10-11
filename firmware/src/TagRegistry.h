#pragma once

#include <ArduinoJson.h>
#include <map>
#include <string>

constexpr float kVolumeUseDefault = -2.0f;  // not stored; use Settings::defaultVolume
constexpr float kVolumeNoChange = -1.0f;    // stored as -1; do not send volume to HA

struct Tag {
  std::string name;
  std::string uri;
  std::string contentType = "music";
  std::string player;  // empty = use Settings::defaultPlayer
  float volume = kVolumeUseDefault;
  bool shuffle = false;
};

class TagRegistry {
 public:
  static constexpr size_t kMaxTags = 200;
  static constexpr int kVersion = 1;
  enum class UpsertResult { Ok, Invalid, Full };

  // Replaces the contents with the file's tags. On any error returns false,
  // sets *err and leaves the registry unchanged.
  bool loadJson(const std::string& json, std::string* err = nullptr);
  std::string toJson() const;

  bool find(const std::string& uid, Tag& out) const;
  UpsertResult upsert(const std::string& uid, const Tag& tag, std::string* err = nullptr);
  bool remove(const std::string& uid);
  const std::map<std::string, Tag>& all() const { return tags_; }
  size_t size() const { return tags_.size(); }

  static std::string normalizeUid(const std::string& uid);
  static bool validate(const Tag& tag, std::string* err);
  static bool tagFromJson(JsonVariantConst v, Tag& out, std::string* err);
  static void tagToJson(const Tag& tag, JsonObject out);

 private:
  std::map<std::string, Tag> tags_;
};
