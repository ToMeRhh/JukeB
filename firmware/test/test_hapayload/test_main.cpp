#include <unity.h>
#include <string>
#include "HaPayload.h"

void setUp() {}
void tearDown() {}

static Settings settings() {
  Settings s;
  s.players = {"media_player.kitchen", "media_player.kids"};
  s.defaultPlayer = "media_player.kitchen";
  s.defaultVolume = 0.25f;
  return s;
}

static Tag tag() {
  Tag t;
  t.name = "Blue dinosaur";
  t.uri = "media-source://media_source/media/1.mp3";
  return t;
}

void test_effective_player_falls_back_to_default() {
  Tag t = tag();
  TEST_ASSERT_EQUAL_STRING("media_player.kitchen", HaPayload::effectivePlayer(t, settings()).c_str());
  t.player = "media_player.kids";
  TEST_ASSERT_EQUAL_STRING("media_player.kids", HaPayload::effectivePlayer(t, settings()).c_str());
}

void test_effective_volume() {
  Tag t = tag();
  TEST_ASSERT_EQUAL_FLOAT(0.25f, HaPayload::effectiveVolume(t, settings()));
  t.volume = 0.5f;
  TEST_ASSERT_EQUAL_FLOAT(0.5f, HaPayload::effectiveVolume(t, settings()));
  t.volume = kVolumeNoChange;
  TEST_ASSERT_EQUAL_FLOAT(kVolumeNoChange, HaPayload::effectiveVolume(t, settings()));
}

void test_play_payload_with_default_volume_and_player() {
  TEST_ASSERT_EQUAL_STRING(
      "{\"event\":\"play\",\"uid\":\"04:A3:7F:12\",\"name\":\"Blue dinosaur\","
      "\"uri\":\"media-source://media_source/media/1.mp3\",\"content_type\":\"music\","
      "\"player\":\"media_player.kitchen\",\"volume\":0.25,\"shuffle\":false}",
      HaPayload::play("04:A3:7F:12", tag(), settings()).c_str());
}

void test_play_payload_omits_volume_when_no_change() {
  Tag t = tag();
  t.volume = kVolumeNoChange;
  t.shuffle = true;
  t.player = "media_player.kids";
  t.contentType = "playlist";
  TEST_ASSERT_EQUAL_STRING(
      "{\"event\":\"play\",\"uid\":\"04:A3:7F:12\",\"name\":\"Blue dinosaur\","
      "\"uri\":\"media-source://media_source/media/1.mp3\",\"content_type\":\"playlist\","
      "\"player\":\"media_player.kids\",\"shuffle\":true}",
      HaPayload::play("04:A3:7F:12", t, settings()).c_str());
}

void test_control_payloads() {
  TEST_ASSERT_EQUAL_STRING(
      "{\"event\":\"pause\",\"uid\":\"04:A3:7F:12\",\"player\":\"media_player.kitchen\"}",
      HaPayload::control("pause", "04:A3:7F:12", tag(), settings()).c_str());
  TEST_ASSERT_EQUAL_STRING(
      "{\"event\":\"stop\",\"uid\":\"04:A3:7F:12\",\"player\":\"media_player.kitchen\"}",
      HaPayload::control("stop", "04:A3:7F:12", tag(), settings()).c_str());
}

void test_test_payload() {
  TEST_ASSERT_EQUAL_STRING("{\"event\":\"test\"}", HaPayload::test().c_str());
}

void test_has_player_false_when_nothing_configured() {
  Settings s;  // no players, no default
  Tag t = tag();
  TEST_ASSERT_FALSE(HaPayload::hasPlayer(t, s));
  t.player = "media_player.kids";
  TEST_ASSERT_TRUE(HaPayload::hasPlayer(t, s));
  t.player = "";
  TEST_ASSERT_TRUE(HaPayload::hasPlayer(t, settings()));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_has_player_false_when_nothing_configured);
  RUN_TEST(test_effective_player_falls_back_to_default);
  RUN_TEST(test_effective_volume);
  RUN_TEST(test_play_payload_with_default_volume_and_player);
  RUN_TEST(test_play_payload_omits_volume_when_no_change);
  RUN_TEST(test_control_payloads);
  RUN_TEST(test_test_payload);
  return UNITY_END();
}
