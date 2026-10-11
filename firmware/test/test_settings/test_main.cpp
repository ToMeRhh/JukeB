#include <unity.h>
#include <ArduinoJson.h>
#include <string>
#include "Settings.h"

void setUp() {}
void tearDown() {}

static JsonDocument parse(const std::string& s) {
  JsonDocument d;
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, s), "toJson produced invalid JSON");
  return d;
}

void test_defaults_from_empty_object() {
  Settings s;
  std::string err;
  TEST_ASSERT_TRUE(s.loadJson("{}", &err));
  TEST_ASSERT_EQUAL_STRING("", s.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("jukebox", s.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING("http://homeassistant.local:8123", s.haUrl.c_str());
  TEST_ASSERT_EQUAL_STRING("jukebox", s.webhookId.c_str());
  TEST_ASSERT_EQUAL(0, s.players.size());
  TEST_ASSERT_EQUAL_FLOAT(0.3f, s.defaultVolume);
  TEST_ASSERT_EQUAL(static_cast<int>(Settings::OnRemove::None), static_cast<int>(s.onRemove));
  TEST_ASSERT_EQUAL_UINT32(1500, s.pauseGraceMs);
}

void test_webhook_url() {
  Settings s;
  s.haUrl = "http://192.168.1.10:8123";
  s.webhookId = "abc";
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.10:8123/api/webhook/abc", s.webhookUrl().c_str());
}

void test_normalize_url() {
  TEST_ASSERT_EQUAL_STRING("http://homeassistant.local:8123", Settings::normalizeUrl(" homeassistant.local:8123/ ").c_str());
  TEST_ASSERT_EQUAL_STRING("https://ha.example.com", Settings::normalizeUrl("https://ha.example.com//").c_str());
  TEST_ASSERT_EQUAL_STRING("", Settings::normalizeUrl("  ").c_str());
}

void test_to_json_masks_password_only_when_set() {
  Settings s;
  s.wifiSsid = "net";
  JsonDocument d = parse(s.toJson(true));
  TEST_ASSERT_EQUAL_STRING("", d["wifi_pass"].as<const char*>());
  s.wifiPass = "secret";
  d = parse(s.toJson(true));
  TEST_ASSERT_EQUAL_STRING("********", d["wifi_pass"].as<const char*>());
  d = parse(s.toJson(false));
  TEST_ASSERT_EQUAL_STRING("secret", d["wifi_pass"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("net", d["wifi_ssid"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("none", d["on_remove"].as<const char*>());
  TEST_ASSERT_EQUAL(1, d["version"].as<int>());
}

void test_apply_masked_password_keeps_current() {
  Settings s;
  s.wifiPass = "secret";
  std::string err;
  TEST_ASSERT_TRUE(s.applyJson("{\"wifi_pass\":\"********\",\"wifi_ssid\":\"new\"}", &err));
  TEST_ASSERT_EQUAL_STRING("secret", s.wifiPass.c_str());
  TEST_ASSERT_EQUAL_STRING("new", s.wifiSsid.c_str());
  TEST_ASSERT_TRUE(s.applyJson("{\"wifi_pass\":\"other\"}", &err));
  TEST_ASSERT_EQUAL_STRING("other", s.wifiPass.c_str());
}

void test_clamping() {
  Settings s;
  std::string err;
  TEST_ASSERT_TRUE(s.applyJson("{\"default_volume\":1.7,\"pause_grace_ms\":50}", &err));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.defaultVolume);
  TEST_ASSERT_EQUAL_UINT32(200, s.pauseGraceMs);
  TEST_ASSERT_TRUE(s.applyJson("{\"default_volume\":-3,\"pause_grace_ms\":99999}", &err));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.defaultVolume);
  TEST_ASSERT_EQUAL_UINT32(30000, s.pauseGraceMs);
}

void test_on_remove_parsing() {
  Settings s;
  std::string err;
  TEST_ASSERT_TRUE(s.applyJson("{\"on_remove\":\"pause\"}", &err));
  TEST_ASSERT_EQUAL(static_cast<int>(Settings::OnRemove::Pause), static_cast<int>(s.onRemove));
  TEST_ASSERT_TRUE(s.applyJson("{\"on_remove\":\"stop\"}", &err));
  TEST_ASSERT_EQUAL(static_cast<int>(Settings::OnRemove::Stop), static_cast<int>(s.onRemove));
  TEST_ASSERT_TRUE(s.applyJson("{\"on_remove\":\"bogus\"}", &err));
  TEST_ASSERT_EQUAL(static_cast<int>(Settings::OnRemove::None), static_cast<int>(s.onRemove));
  TEST_ASSERT_EQUAL_STRING("stop", Settings::onRemoveToString(Settings::OnRemove::Stop));
}

void test_players_trimmed_and_deduplicated() {
  Settings s;
  std::string err;
  TEST_ASSERT_TRUE(s.applyJson("{\"players\":[\" media_player.a \",\"media_player.a\",\"\",\"media_player.b\",7]}", &err));
  TEST_ASSERT_EQUAL(2, s.players.size());
  TEST_ASSERT_EQUAL_STRING("media_player.a", s.players[0].c_str());
  TEST_ASSERT_EQUAL_STRING("media_player.b", s.players[1].c_str());
}

void test_default_player_must_be_listed() {
  Settings s;
  std::string err;
  TEST_ASSERT_FALSE(s.applyJson("{\"players\":[\"media_player.a\"],\"default_player\":\"media_player.z\"}", &err));
  TEST_ASSERT_EQUAL_STRING("default_player must be one of players", err.c_str());
  TEST_ASSERT_EQUAL(0, s.players.size());  // rejected update changes nothing
  TEST_ASSERT_TRUE(s.applyJson("{\"players\":[\"media_player.a\"],\"default_player\":\"media_player.a\"}", &err));
  TEST_ASSERT_EQUAL_STRING("media_player.a", s.defaultPlayer.c_str());
  TEST_ASSERT_TRUE(s.applyJson("{\"default_player\":\"\"}", &err));
}

void test_required_fields() {
  Settings s;
  std::string err;
  TEST_ASSERT_FALSE(s.applyJson("{\"ha_url\":\" \"}", &err));
  TEST_ASSERT_EQUAL_STRING("ha_url is required", err.c_str());
  TEST_ASSERT_FALSE(s.applyJson("{\"webhook_id\":\"\"}", &err));
  TEST_ASSERT_EQUAL_STRING("webhook_id is required", err.c_str());
  TEST_ASSERT_TRUE(s.applyJson("{\"hostname\":\"  \"}", &err));
  TEST_ASSERT_EQUAL_STRING("jukebox", s.hostname.c_str());
}

void test_apply_ignores_bad_grace_type() {
  Settings s;
  s.pauseGraceMs = 2500;
  std::string err;
  TEST_ASSERT_TRUE(s.applyJson("{\"pause_grace_ms\":\"soon\"}", &err));
  TEST_ASSERT_EQUAL_UINT32(2500, s.pauseGraceMs);
  TEST_ASSERT_TRUE(s.applyJson("{\"pause_grace_ms\":-5}", &err));
  TEST_ASSERT_EQUAL_UINT32(2500, s.pauseGraceMs);
}

void test_parse_error_leaves_settings_unchanged() {
  Settings s;
  s.wifiSsid = "keep";
  std::string err;
  TEST_ASSERT_FALSE(s.applyJson("{oops", &err));
  TEST_ASSERT_TRUE(err.rfind("parse error", 0) == 0);
  TEST_ASSERT_EQUAL_STRING("keep", s.wifiSsid.c_str());
  TEST_ASSERT_FALSE(s.loadJson("[1,2]", &err));
  TEST_ASSERT_EQUAL_STRING("root must be an object", err.c_str());
  TEST_ASSERT_EQUAL_STRING("keep", s.wifiSsid.c_str());
}

void test_load_resets_to_defaults_before_applying() {
  Settings s;
  s.wifiSsid = "old";
  s.pauseGraceMs = 9000;
  std::string err;
  TEST_ASSERT_TRUE(s.loadJson("{\"wifi_ssid\":\"new\"}", &err));
  TEST_ASSERT_EQUAL_STRING("new", s.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_UINT32(1500, s.pauseGraceMs);
}

void test_round_trip() {
  Settings s;
  std::string err;
  TEST_ASSERT_TRUE(s.applyJson(
      "{\"wifi_ssid\":\"net\",\"wifi_pass\":\"pw\",\"ha_url\":\"http://ha:8123\",\"webhook_id\":\"w\","
      "\"players\":[\"media_player.a\"],\"default_player\":\"media_player.a\",\"default_volume\":0.5,"
      "\"on_remove\":\"pause\",\"pause_grace_ms\":3000,\"hostname\":\"box\"}", &err));
  Settings t;
  TEST_ASSERT_TRUE(t.loadJson(s.toJson(false), &err));
  TEST_ASSERT_EQUAL_STRING("pw", t.wifiPass.c_str());
  TEST_ASSERT_EQUAL_STRING("http://ha:8123", t.haUrl.c_str());
  TEST_ASSERT_EQUAL_STRING("media_player.a", t.defaultPlayer.c_str());
  TEST_ASSERT_EQUAL_FLOAT(0.5f, t.defaultVolume);
  TEST_ASSERT_EQUAL(static_cast<int>(Settings::OnRemove::Pause), static_cast<int>(t.onRemove));
  TEST_ASSERT_EQUAL_UINT32(3000, t.pauseGraceMs);
  TEST_ASSERT_EQUAL_STRING("box", t.hostname.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_from_empty_object);
  RUN_TEST(test_webhook_url);
  RUN_TEST(test_normalize_url);
  RUN_TEST(test_to_json_masks_password_only_when_set);
  RUN_TEST(test_apply_masked_password_keeps_current);
  RUN_TEST(test_clamping);
  RUN_TEST(test_on_remove_parsing);
  RUN_TEST(test_players_trimmed_and_deduplicated);
  RUN_TEST(test_default_player_must_be_listed);
  RUN_TEST(test_required_fields);
  RUN_TEST(test_apply_ignores_bad_grace_type);
  RUN_TEST(test_parse_error_leaves_settings_unchanged);
  RUN_TEST(test_load_resets_to_defaults_before_applying);
  RUN_TEST(test_round_trip);
  return UNITY_END();
}
