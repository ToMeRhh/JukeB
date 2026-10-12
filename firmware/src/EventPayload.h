#pragma once

#include <cstdint>
#include <string>

// Builds the JSON bodies the box posts to the Jukebox webhook.
namespace EventPayload {

struct BoxInfo {
  std::string boxId;
  std::string version;
};

std::string present(const BoxInfo& box, const std::string& uid);
std::string absent(const BoxInfo& box, const std::string& uid);
std::string heartbeat(const BoxInfo& box, uint32_t uptimeS, int rssi);

}  // namespace EventPayload
