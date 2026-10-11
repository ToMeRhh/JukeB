#include <unity.h>
#include "RetrySchedule.h"

void setUp() {}
void tearDown() {}

void test_first_attempt_is_due_immediately() {
  RetrySchedule r(30000);
  TEST_ASSERT_TRUE(r.due(0));
  TEST_ASSERT_TRUE(r.due(5));
}

void test_not_due_until_interval_elapsed() {
  RetrySchedule r(30000);
  r.mark(1000);
  TEST_ASSERT_FALSE(r.due(1000));
  TEST_ASSERT_FALSE(r.due(30999));
  TEST_ASSERT_TRUE(r.due(31000));
}

void test_reset_makes_it_due_again() {
  RetrySchedule r(30000);
  r.mark(1000);
  r.reset();
  TEST_ASSERT_TRUE(r.due(1001));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_attempt_is_due_immediately);
  RUN_TEST(test_not_due_until_interval_elapsed);
  RUN_TEST(test_reset_makes_it_due_again);
  return UNITY_END();
}
