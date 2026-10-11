#pragma once

#include <string>

#include "Settings.h"
#include "TagRegistry.h"

// Builds the JSON bodies sent to the Home Assistant webhook. No I/O here.
namespace HaPayload {

std::string effectivePlayer(const Tag& tag, const Settings& s);
// Returns kVolumeNoChange when no volume must be sent.
float effectiveVolume(const Tag& tag, const Settings& s);
// False when neither the tag nor the settings name a player: nothing can be played.
bool hasPlayer(const Tag& tag, const Settings& s);

std::string play(const std::string& uid, const Tag& tag, const Settings& s);
// event is "pause" or "stop".
std::string control(const std::string& event, const std::string& uid, const Tag& tag, const Settings& s);
std::string test();

}  // namespace HaPayload
