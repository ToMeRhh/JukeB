#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "Settings.h"
#include "TagRegistry.h"

struct HaResult {
  bool any = false;  // false until the first send
  bool ok = false;
  int httpCode = 0;
  std::string message;
  uint32_t atMillis = 0;
};

// Work the web task asks loop() to do (anything that blocks or touches the network).
struct Command {
  enum class Type { PlayTag, TestWebhook, Reboot, SettingsChanged };
  Type type;
  std::string uid;
};

struct AppState {
  TagRegistry registry;
  Settings settings;

  std::string lastScannedUid;
  uint32_t lastScannedAt = 0;
  bool lastScannedKnown = false;

  HaResult lastResult;
  std::string warning;  // config problems to show in the UI
  bool readerFound = false;
  uint8_t readerVersion = 0;

  std::deque<Command> commands;
  std::mutex mutex;  // guards every field above
};
