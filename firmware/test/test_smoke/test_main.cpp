#include <unity.h>
#include <ArduinoJson.h>
#include <string>

void setUp() {}
void tearDown() {}

void test_arduinojson_roundtrip() {
  JsonDocument doc;
  doc["a"] = 1;
  std::string out;
  serializeJson(doc, out);
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", out.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_arduinojson_roundtrip);
  return UNITY_END();
}
