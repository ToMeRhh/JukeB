#include "Storage.h"

#include <Arduino.h>
#include <LittleFS.h>

bool Storage::begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[storage] LittleFS mount failed");
    return false;
  }
  return true;
}

bool Storage::readFile(const char* path, std::string& out) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out.clear();
  out.reserve(f.size());
  uint8_t buf[256];
  while (f.available()) {
    const size_t n = f.read(buf, sizeof(buf));
    out.append(reinterpret_cast<const char*>(buf), n);
  }
  f.close();
  return true;
}

bool Storage::writeFile(const char* path, const std::string& data) {
  const String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  f.close();
  if (written != data.size()) {
    LittleFS.remove(tmp);
    return false;
  }
  return LittleFS.rename(tmp, path);  // littlefs rename replaces the destination atomically
}

void Storage::quarantine(const char* path) {
  const String bad = String(path) + ".bad";
  LittleFS.remove(bad);
  LittleFS.rename(path, bad);
  Serial.printf("[storage] %s is corrupt, moved to %s\n", path, bad.c_str());
}

bool Storage::loadRegistry(TagRegistry& registry, std::string& warning) {
  std::string json;
  if (!readFile(kTagsPath, json)) return false;
  std::string err;
  if (registry.loadJson(json, &err)) return true;
  warning = std::string("tags.json was corrupt (") + err + "); started with no tags";
  quarantine(kTagsPath);
  return false;
}

bool Storage::saveRegistry(const TagRegistry& registry) {
  return writeFile(kTagsPath, registry.toJson());
}

bool Storage::loadSettings(Settings& settings, std::string& warning) {
  std::string json;
  if (!readFile(kSettingsPath, json)) return false;
  std::string err;
  if (settings.loadJson(json, &err)) return true;
  warning = std::string("settings.json was corrupt (") + err + "); using defaults";
  quarantine(kSettingsPath);
  return false;
}

bool Storage::saveSettings(const Settings& settings) {
  return writeFile(kSettingsPath, settings.toJson(false));
}
