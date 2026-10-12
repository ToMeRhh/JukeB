#pragma once

#include <cstdint>
#include <string>

// One reader poll result in, at most one event out.
struct PresenceEvent {
  enum class Type { None, Present, Absent };
  Type type = Type::None;
  std::string uid;
};

// Hardware-level debounce only. Present after N consecutive reads of the same
// UID, Absent after M consecutive polls without that UID. Counts polls, not
// time, so a slow loop iteration (blocking HTTP) cannot fake a removal.
class PresenceDebouncer {
 public:
  explicit PresenceDebouncer(uint8_t hitsToPresent = 2, uint8_t missesToAbsent = 3);

  PresenceEvent update(bool seen, const std::string& uid);

  bool present() const { return !current_.empty(); }
  const std::string& currentUid() const { return current_; }

 private:
  uint8_t hitsNeeded_;
  uint8_t missesNeeded_;
  std::string current_;    // UID reported Present, empty when none
  std::string candidate_;  // UID of the latest read
  uint8_t candidateHits_ = 0;
  uint8_t misses_ = 0;
};
