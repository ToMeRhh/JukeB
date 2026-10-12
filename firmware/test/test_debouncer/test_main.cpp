#include <unity.h>

#include <string>

#include "PresenceDebouncer.h"

using Type = PresenceEvent::Type;

void setUp() {}
void tearDown() {}

void test_single_read_is_not_present() {
  PresenceDebouncer d;
  TEST_ASSERT_EQUAL(Type::None, d.update(true, "AA").type);
  TEST_ASSERT_EQUAL(Type::None, d.update(false, "").type);
  TEST_ASSERT_FALSE(d.present());
}

void test_two_consecutive_reads_emit_present() {
  PresenceDebouncer d;
  TEST_ASSERT_EQUAL(Type::None, d.update(true, "AA").type);
  const PresenceEvent ev = d.update(true, "AA");
  TEST_ASSERT_EQUAL(Type::Present, ev.type);
  TEST_ASSERT_EQUAL_STRING("AA", ev.uid.c_str());
  TEST_ASSERT_TRUE(d.present());
  TEST_ASSERT_EQUAL_STRING("AA", d.currentUid().c_str());
  // staying on the reader emits nothing more
  TEST_ASSERT_EQUAL(Type::None, d.update(true, "AA").type);
}

void test_three_misses_emit_absent() {
  PresenceDebouncer d;
  d.update(true, "AA");
  d.update(true, "AA");
  TEST_ASSERT_EQUAL(Type::None, d.update(false, "").type);
  TEST_ASSERT_EQUAL(Type::None, d.update(false, "").type);
  const PresenceEvent ev = d.update(false, "");
  TEST_ASSERT_EQUAL(Type::Absent, ev.type);
  TEST_ASSERT_EQUAL_STRING("AA", ev.uid.c_str());
  TEST_ASSERT_FALSE(d.present());
}

void test_two_misses_then_read_is_a_flicker() {
  PresenceDebouncer d;
  d.update(true, "AA");
  d.update(true, "AA");
  d.update(false, "");
  d.update(false, "");
  TEST_ASSERT_EQUAL(Type::None, d.update(true, "AA").type);
  TEST_ASSERT_EQUAL(Type::None, d.update(false, "").type);
  TEST_ASSERT_EQUAL(Type::None, d.update(false, "").type);
  TEST_ASSERT_TRUE(d.present());  // miss counter was reset by the read
}

void test_interleaved_reads_do_not_count_as_consecutive() {
  PresenceDebouncer d;
  TEST_ASSERT_EQUAL(Type::None, d.update(true, "AA").type);
  TEST_ASSERT_EQUAL(Type::None, d.update(false, "").type);
  TEST_ASSERT_EQUAL(Type::None, d.update(true, "AA").type);
  TEST_ASSERT_EQUAL(Type::Present, d.update(true, "AA").type);
}

void test_switch_tag_emits_absent_then_present() {
  PresenceDebouncer d;
  d.update(true, "AA");
  d.update(true, "AA");
  TEST_ASSERT_EQUAL(Type::None, d.update(true, "BB").type);
  TEST_ASSERT_EQUAL(Type::None, d.update(true, "BB").type);
  PresenceEvent ev = d.update(true, "BB");
  TEST_ASSERT_EQUAL(Type::Absent, ev.type);
  TEST_ASSERT_EQUAL_STRING("AA", ev.uid.c_str());
  ev = d.update(true, "BB");
  TEST_ASSERT_EQUAL(Type::Present, ev.type);
  TEST_ASSERT_EQUAL_STRING("BB", ev.uid.c_str());
  TEST_ASSERT_EQUAL_STRING("BB", d.currentUid().c_str());
}

void test_custom_thresholds() {
  PresenceDebouncer d(1, 1);
  TEST_ASSERT_EQUAL(Type::Present, d.update(true, "AA").type);
  TEST_ASSERT_EQUAL(Type::Absent, d.update(false, "").type);
}

void test_replace_after_absent_places_again() {
  PresenceDebouncer d;
  d.update(true, "AA");
  d.update(true, "AA");
  for (int i = 0; i < 3; ++i) d.update(false, "");
  TEST_ASSERT_FALSE(d.present());
  d.update(true, "AA");
  TEST_ASSERT_EQUAL(Type::Present, d.update(true, "AA").type);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_read_is_not_present);
  RUN_TEST(test_two_consecutive_reads_emit_present);
  RUN_TEST(test_three_misses_emit_absent);
  RUN_TEST(test_two_misses_then_read_is_a_flicker);
  RUN_TEST(test_interleaved_reads_do_not_count_as_consecutive);
  RUN_TEST(test_switch_tag_emits_absent_then_present);
  RUN_TEST(test_custom_thresholds);
  RUN_TEST(test_replace_after_absent_places_again);
  return UNITY_END();
}
