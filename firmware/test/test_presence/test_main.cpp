#include <unity.h>
#include <string>
#include "PresenceTracker.h"

void setUp() {}
void tearDown() {}

static const std::string A = "04:A3:7F:12";
static const std::string B = "AA:BB:CC:DD";

static PresenceTracker tracker() {
  PresenceTracker t;
  t.setGraceMs(1500);
  return t;
}

// Feeds "seen uid" polls every 100 ms from `from` to `to` inclusive. Returns the last result.
static PresenceTracker::Result seenRange(PresenceTracker& t, const std::string& uid, uint32_t from, uint32_t to) {
  PresenceTracker::Result r;
  for (uint32_t now = from; now <= to; now += 100) r = t.update(true, uid, now);
  return r;
}
static PresenceTracker::Result missRange(PresenceTracker& t, uint32_t from, uint32_t to) {
  PresenceTracker::Result r;
  for (uint32_t now = from; now <= to; now += 100) r = t.update(false, "", now);
  return r;
}

void test_single_sighting_is_not_placed() {
  PresenceTracker t = tracker();
  PresenceTracker::Result r = t.update(true, A, 0);
  TEST_ASSERT_FALSE(r.placed);
  TEST_ASSERT_FALSE(t.present());
}

void test_two_consecutive_sightings_place() {
  PresenceTracker t = tracker();
  t.update(true, A, 0);
  PresenceTracker::Result r = t.update(true, A, 100);
  TEST_ASSERT_TRUE(r.placed);
  TEST_ASSERT_EQUAL_STRING(A.c_str(), r.placedUid.c_str());
  TEST_ASSERT_FALSE(r.removed);
  TEST_ASSERT_TRUE(t.present());
  TEST_ASSERT_EQUAL_STRING(A.c_str(), t.currentUid().c_str());
  // Further sightings produce no events.
  r = t.update(true, A, 200);
  TEST_ASSERT_FALSE(r.placed);
  TEST_ASSERT_FALSE(r.removed);
}

void test_miss_between_sightings_resets_count() {
  PresenceTracker t = tracker();
  t.update(true, A, 0);
  t.update(false, "", 100);
  PresenceTracker::Result r = t.update(true, A, 200);
  TEST_ASSERT_FALSE(r.placed);
  r = t.update(true, A, 300);
  TEST_ASSERT_TRUE(r.placed);
}

void test_removed_after_grace() {
  PresenceTracker t = tracker();
  seenRange(t, A, 0, 1000);  // last seen at 1000
  PresenceTracker::Result r = missRange(t, 1100, 2400);
  TEST_ASSERT_FALSE(r.removed);
  TEST_ASSERT_TRUE(t.present());
  r = t.update(false, "", 2500);  // 1500 ms after last sighting
  TEST_ASSERT_TRUE(r.removed);
  TEST_ASSERT_EQUAL_STRING(A.c_str(), r.removedUid.c_str());
  TEST_ASSERT_FALSE(r.placed);
  TEST_ASSERT_FALSE(t.present());
}

void test_brief_lift_within_grace_is_ignored() {
  PresenceTracker t = tracker();
  seenRange(t, A, 0, 1000);
  missRange(t, 1100, 1800);                       // lifted for 800 ms
  PresenceTracker::Result r = t.update(true, A, 1900);  // put back
  TEST_ASSERT_FALSE(r.placed);
  TEST_ASSERT_FALSE(r.removed);
  r = missRange(t, 2000, 3300);                   // grace counts from 1900 now
  TEST_ASSERT_FALSE(r.removed);
  r = t.update(false, "", 3400);
  TEST_ASSERT_TRUE(r.removed);
}

void test_replace_after_removal_places_again() {
  PresenceTracker t = tracker();
  seenRange(t, A, 0, 500);
  missRange(t, 600, 1900);                       // 1400 ms since last sighting: still present
  PresenceTracker::Result r = t.update(false, "", 2000);  // 1500 ms: removed
  TEST_ASSERT_TRUE(r.removed);
  r = seenRange(t, A, 2100, 2200);
  TEST_ASSERT_TRUE(r.placed);
  TEST_ASSERT_EQUAL_STRING(A.c_str(), r.placedUid.c_str());
}

void test_swap_emits_removed_then_placed() {
  PresenceTracker t = tracker();
  seenRange(t, A, 0, 500);
  PresenceTracker::Result r = t.update(true, B, 600);
  TEST_ASSERT_FALSE(r.placed);
  TEST_ASSERT_FALSE(r.removed);
  r = t.update(true, B, 700);
  TEST_ASSERT_TRUE(r.removed);
  TEST_ASSERT_EQUAL_STRING(A.c_str(), r.removedUid.c_str());
  TEST_ASSERT_TRUE(r.placed);
  TEST_ASSERT_EQUAL_STRING(B.c_str(), r.placedUid.c_str());
  TEST_ASSERT_EQUAL_STRING(B.c_str(), t.currentUid().c_str());
}

void test_grace_setting_is_respected() {
  PresenceTracker t = tracker();
  t.setGraceMs(300);
  seenRange(t, A, 0, 200);
  PresenceTracker::Result r = t.update(false, "", 400);
  TEST_ASSERT_FALSE(r.removed);
  r = t.update(false, "", 500);
  TEST_ASSERT_TRUE(r.removed);
}

void test_long_gap_between_polls_does_not_count_toward_grace() {
  // loop() was blocked (e.g. a 20 s failed webhook POST) between two polls.
  PresenceTracker t = tracker();
  seenRange(t, A, 0, 1000);
  PresenceTracker::Result r = t.update(false, "", 21000);  // first poll after the gap: one missed read
  TEST_ASSERT_FALSE(r.removed);
  TEST_ASSERT_TRUE(t.present());
  r = missRange(t, 21100, 22400);  // grace counts from the end of the gap
  TEST_ASSERT_FALSE(r.removed);
  r = t.update(false, "", 22500);
  TEST_ASSERT_TRUE(r.removed);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_long_gap_between_polls_does_not_count_toward_grace);
  RUN_TEST(test_single_sighting_is_not_placed);
  RUN_TEST(test_two_consecutive_sightings_place);
  RUN_TEST(test_miss_between_sightings_resets_count);
  RUN_TEST(test_removed_after_grace);
  RUN_TEST(test_brief_lift_within_grace_is_ignored);
  RUN_TEST(test_replace_after_removal_places_again);
  RUN_TEST(test_swap_emits_removed_then_placed);
  RUN_TEST(test_grace_setting_is_respected);
  return UNITY_END();
}
