#include <unity.h>

#include <string>

#include "ReplyParser.h"

void setUp() {}
void tearDown() {}

void test_known() {
  const ReplyParser::Reply r = ReplyParser::parse(200, "{\"ok\":true,\"known\":true}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.known);
  TEST_ASSERT_EQUAL_STRING("ok, known", r.message.c_str());
}

void test_unknown() {
  const ReplyParser::Reply r = ReplyParser::parse(200, "{\"ok\":true,\"known\":false}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FALSE(r.known);
  TEST_ASSERT_EQUAL_STRING("ok, unknown tag", r.message.c_str());
}

void test_ok_without_known() {
  const ReplyParser::Reply r = ReplyParser::parse(200, "{\"ok\":true}");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FALSE(r.known);
  TEST_ASSERT_EQUAL_STRING("ok", r.message.c_str());
}

void test_generic_200_without_json_is_not_ok() {
  // Home Assistant answers an empty 200 when no integration owns the webhook.
  const ReplyParser::Reply r = ReplyParser::parse(200, "");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("HTTP 200 but no JukeB reply (is the integration installed and the webhook id right?)", r.message.c_str());
}

void test_400_with_error() {
  const ReplyParser::Reply r = ReplyParser::parse(400, "{\"ok\":false,\"error\":\"unknown event\"}");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("HTTP 400: unknown event", r.message.c_str());
}

void test_404_plain() {
  const ReplyParser::Reply r = ReplyParser::parse(404, "<html>not found</html>");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("HTTP 404", r.message.c_str());
}

void test_transport_error() {
  const ReplyParser::Reply r = ReplyParser::parse(-1, "");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("connection failed", r.message.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_known);
  RUN_TEST(test_unknown);
  RUN_TEST(test_ok_without_known);
  RUN_TEST(test_generic_200_without_json_is_not_ok);
  RUN_TEST(test_400_with_error);
  RUN_TEST(test_404_plain);
  RUN_TEST(test_transport_error);
  return UNITY_END();
}
