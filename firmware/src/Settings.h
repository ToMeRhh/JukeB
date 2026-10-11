#pragma once

#include <ArduinoJson.h>
#include <cstdint>
#include <string>
#include <vector>

struct Settings {
  static constexpr int kVersion = 1;
  static constexpr const char* kMaskedPassword = "********";
  enum class OnRemove { None, Pause, Stop };

  std::string wifiSsid;
  std::string wifiPass;
  std::string hostname = "jukebox";
  std::string haUrl = "http://homeassistant.local:8123";
  std::string webhookId = "jukebox";
  std::vector<std::string> players;
  std::string defaultPlayer;
  float defaultVolume = 0.3f;
  OnRemove onRemove = OnRemove::None;
  uint32_t pauseGraceMs = 1500;

  // Parse a whole file: starts from defaults, applies every field present.
  // On error returns false, sets *err and leaves *this unchanged.
  bool loadJson(const std::string& json, std::string* err = nullptr);
  // Apply a partial update (API body) on top of the current values.
  // A masked password keeps the current one. Same error contract as loadJson.
  bool applyJson(const std::string& json, std::string* err = nullptr);
  std::string toJson(bool maskPassword) const;

  std::string webhookUrl() const;

  static std::string normalizeUrl(const std::string& url);
  static const char* onRemoveToString(OnRemove v);
  static bool onRemoveFromString(const std::string& s, OnRemove& out);

 private:
  bool applyObject(JsonObjectConst o, std::string* err);
};
