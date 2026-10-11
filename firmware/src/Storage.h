#pragma once

#include <string>

#include "Settings.h"
#include "TagRegistry.h"

// LittleFS persistence for tags.json and settings.json.
// A corrupt file is renamed to <name>.bad and defaults are used.
class Storage {
 public:
  static constexpr const char* kTagsPath = "/tags.json";
  static constexpr const char* kSettingsPath = "/settings.json";

  bool begin();
  // Return true when the file was loaded. False means defaults are in use;
  // `warning` is set only if the file existed and was unreadable.
  bool loadRegistry(TagRegistry& registry, std::string& warning);
  bool saveRegistry(const TagRegistry& registry);
  bool loadSettings(Settings& settings, std::string& warning);
  bool saveSettings(const Settings& settings);

 private:
  bool readFile(const char* path, std::string& out);
  bool writeFile(const char* path, const std::string& data);
  void quarantine(const char* path);
};
