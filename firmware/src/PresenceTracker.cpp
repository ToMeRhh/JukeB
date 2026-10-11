#include "PresenceTracker.h"

PresenceTracker::Result PresenceTracker::update(bool seen, const std::string& uid, uint32_t nowMs) {
  Result r;

  // Time during which nobody polled (the caller was blocked) must not count as
  // "tag absent": shift the last-seen mark forward by the gap.
  if (hasUpdated_ && present_) {
    const uint32_t gap = nowMs - lastUpdateMs_;
    if (gap > maxGapMs_) lastSeenMs_ += gap;
  }
  hasUpdated_ = true;
  lastUpdateMs_ = nowMs;

  if (seen && present_ && uid == currentUid_) {
    lastSeenMs_ = nowMs;
    candidateHits_ = 0;
    candidateUid_.clear();
    return r;
  }

  if (seen) {
    if (uid == candidateUid_) {
      ++candidateHits_;
    } else {
      candidateUid_ = uid;
      candidateHits_ = 1;
    }
    if (candidateHits_ >= requiredHits_) {
      if (present_) {
        r.removed = true;
        r.removedUid = currentUid_;
      }
      present_ = true;
      currentUid_ = candidateUid_;
      lastSeenMs_ = nowMs;
      candidateHits_ = 0;
      candidateUid_.clear();
      r.placed = true;
      r.placedUid = currentUid_;
    }
    return r;
  }

  // Not seen.
  candidateHits_ = 0;
  candidateUid_.clear();
  if (present_ && (nowMs - lastSeenMs_) >= graceMs_) {
    r.removed = true;
    r.removedUid = currentUid_;
    present_ = false;
    currentUid_.clear();
  }
  return r;
}
