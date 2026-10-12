#include <unity.h>

#include <ArduinoJson.h>
#include <string>

#include "EventPayload.h"

void setUp() {}
void tearDown() {}

static const EventPayload::BoxInfo kBox{"kids-room", "2.0.0"};

static JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, s) == DeserializationError::Ok);
  return doc;
}

void test_present() {
  JsonDocument doc = parse(EventPayload::present(kBox, "04:A3:7F:12"));
  TEST_ASSERT_EQUAL_STRING("present", doc["event"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("04:A3:7F:12", doc["uid"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("kids-room", doc["box_id"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("2.0.0", doc["version"].as<const char*>());
  TEST_ASSERT_EQUAL(4, doc.as<JsonObjectConst>().size());
}

void test_absent() {
  JsonDocument doc = parse(EventPayload::absent(kBox, "AA"));
  TEST_ASSERT_EQUAL_STRING("absent", doc["event"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("AA", doc["uid"].as<const char*>());
}

void test_heartbeat() {
  JsonDocument doc = parse(EventPayload::heartbeat(kBox, 3600, -61));
  TEST_ASSERT_EQUAL_STRING("heartbeat", doc["event"].as<const char*>());
  TEST_ASSERT_EQUAL(3600, doc["uptime_s"].as<uint32_t>());
  TEST_ASSERT_EQUAL(-61, doc["rssi"].as<int>());
  TEST_ASSERT_TRUE(doc["uid"].isNull());
  TEST_ASSERT_EQUAL_STRING("kids-room", doc["box_id"].as<const char*>());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_present);
  RUN_TEST(test_absent);
  RUN_TEST(test_heartbeat);
  return UNITY_END();
}
