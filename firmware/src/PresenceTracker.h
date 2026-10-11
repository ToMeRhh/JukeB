#pragma once

#include <cstdint>
#include <string>

// Turns a stream of "did we see a tag this poll, and which" observations into
// placed/removed events with debounce (consecutive hits) and a removal grace period.
class PresenceTracker {
 public:
  struct Result {
    bool removed = false;
    std::string removedUid;
    bool placed = false;
    std::string placedUid;
  };

  void setGraceMs(uint32_t ms) { graceMs_ = ms; }
  void setRequiredHits(uint8_t hits) { requiredHits_ = hits == 0 ? 1 : hits; }

  Result update(bool seen, const std::string& uid, uint32_t nowMs);

  bool present() const { return present_; }
  const std::string& currentUid() const { return currentUid_; }

 private:
  uint32_t graceMs_ = 1500;
  uint8_t requiredHits_ = 2;

  bool present_ = false;
  std::string currentUid_;
  uint32_t lastSeenMs_ = 0;

  std::string candidateUid_;
  uint8_t candidateHits_ = 0;
};
