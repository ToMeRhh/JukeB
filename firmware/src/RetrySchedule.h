#pragma once

#include <cstdint>

// "Is it time to try again?" with a fixed interval. The first attempt is always due.
class RetrySchedule {
 public:
  explicit RetrySchedule(uint32_t intervalMs) : intervalMs_(intervalMs) {}
  bool due(uint32_t nowMs) const { return !attempted_ || (nowMs - lastMs_) >= intervalMs_; }
  void mark(uint32_t nowMs) { attempted_ = true; lastMs_ = nowMs; }
  void reset() { attempted_ = false; }

 private:
  uint32_t intervalMs_;
  bool attempted_ = false;
  uint32_t lastMs_ = 0;
};
