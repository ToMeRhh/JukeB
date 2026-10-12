#include "PresenceDebouncer.h"

PresenceDebouncer::PresenceDebouncer(uint8_t hitsToPresent, uint8_t missesToAbsent)
    : hitsNeeded_(hitsToPresent), missesNeeded_(missesToAbsent) {}

PresenceEvent PresenceDebouncer::update(bool seen, const std::string& uid) {
  PresenceEvent ev;

  if (seen) {
    if (uid == candidate_) {
      if (candidateHits_ < 255) ++candidateHits_;
    } else {
      candidate_ = uid;
      candidateHits_ = 1;
    }
  } else {
    candidate_.clear();
    candidateHits_ = 0;
  }

  if (!current_.empty()) {
    if (seen && uid == current_) {
      misses_ = 0;
      return ev;
    }
    if (++misses_ >= missesNeeded_) {
      ev.type = PresenceEvent::Type::Absent;
      ev.uid = current_;
      current_.clear();
      misses_ = 0;
    }
    return ev;
  }

  if (seen && candidateHits_ >= hitsNeeded_) {
    current_ = uid;
    misses_ = 0;
    ev.type = PresenceEvent::Type::Present;
    ev.uid = uid;
  }
  return ev;
}
