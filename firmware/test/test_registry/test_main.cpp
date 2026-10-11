#include <unity.h>
#include <ArduinoJson.h>
#include <string>
#include "TagRegistry.h"

void setUp() {}
void tearDown() {}

static Tag sampleTag() {
  Tag t;
  t.name = "Blue dinosaur";
  t.uri = "media-source://media_source/media/1.mp3";
  t.player = "media_player.kitchen";
  t.volume = 0.5f;
  t.shuffle = false;
  return t;
}

void test_empty_registry_serializes() {
  TagRegistry r;
  TEST_ASSERT_EQUAL_STRING("{\"version\":1,\"tags\":{}}", r.toJson().c_str());
  TEST_ASSERT_EQUAL(0, r.size());
}

void test_normalize_uid_uppercases_and_strips_whitespace() {
  TEST_ASSERT_EQUAL_STRING("04:A3:7F:12", TagRegistry::normalizeUid(" 04:a3:7f:12 ").c_str());
  TEST_ASSERT_EQUAL_STRING("", TagRegistry::normalizeUid("   ").c_str());
}

void test_upsert_then_find_roundtrip() {
  TagRegistry r;
  std::string err;
  TEST_ASSERT_EQUAL(static_cast<int>(TagRegistry::UpsertResult::Ok),
                    static_cast<int>(r.upsert("04:a3:7f:12", sampleTag(), &err)));
  Tag out;
  TEST_ASSERT_TRUE(r.find("04:A3:7F:12", out));
  TEST_ASSERT_EQUAL_STRING("Blue dinosaur", out.name.c_str());
  TEST_ASSERT_EQUAL_STRING("media-source://media_source/media/1.mp3", out.uri.c_str());
  TEST_ASSERT_EQUAL_STRING("music", out.contentType.c_str());
  TEST_ASSERT_EQUAL_STRING("media_player.kitchen", out.player.c_str());
  TEST_ASSERT_EQUAL_FLOAT(0.5f, out.volume);
  TEST_ASSERT_FALSE(out.shuffle);
  TEST_ASSERT_FALSE(r.find("FF:FF", out));
}

void test_validate_rejects_bad_tags() {
  std::string err;
  Tag t = sampleTag();
  t.name = "";
  TEST_ASSERT_FALSE(TagRegistry::validate(t, &err));
  TEST_ASSERT_EQUAL_STRING("name is required", err.c_str());

  t = sampleTag();
  t.uri = "";
  TEST_ASSERT_FALSE(TagRegistry::validate(t, &err));
  TEST_ASSERT_EQUAL_STRING("uri is required", err.c_str());

  t = sampleTag();
  t.volume = 1.5f;
  TEST_ASSERT_FALSE(TagRegistry::validate(t, &err));

  t = sampleTag();
  t.volume = kVolumeNoChange;
  TEST_ASSERT_TRUE(TagRegistry::validate(t, &err));
  t.volume = kVolumeUseDefault;
  TEST_ASSERT_TRUE(TagRegistry::validate(t, &err));
}

void test_upsert_invalid_returns_invalid_and_does_not_store() {
  TagRegistry r;
  Tag t = sampleTag();
  t.name = "";
  std::string err;
  TEST_ASSERT_EQUAL(static_cast<int>(TagRegistry::UpsertResult::Invalid),
                    static_cast<int>(r.upsert("04:A3:7F:12", t, &err)));
  TEST_ASSERT_EQUAL(0, r.size());
  TEST_ASSERT_EQUAL(static_cast<int>(TagRegistry::UpsertResult::Invalid),
                    static_cast<int>(r.upsert("  ", sampleTag(), &err)));
  TEST_ASSERT_EQUAL_STRING("uid is required", err.c_str());
}

void test_remove() {
  TagRegistry r;
  r.upsert("04:A3:7F:12", sampleTag());
  TEST_ASSERT_TRUE(r.remove("04:a3:7f:12"));
  TEST_ASSERT_FALSE(r.remove("04:A3:7F:12"));
  TEST_ASSERT_EQUAL(0, r.size());
}

void test_load_parse_error_leaves_registry_unchanged() {
  TagRegistry r;
  r.upsert("04:A3:7F:12", sampleTag());
  std::string err;
  TEST_ASSERT_FALSE(r.loadJson("{not json", &err));
  TEST_ASSERT_TRUE(err.rfind("parse error", 0) == 0);
  TEST_ASSERT_EQUAL(1, r.size());
}

void test_load_rejects_wrong_version() {
  TagRegistry r;
  std::string err;
  TEST_ASSERT_FALSE(r.loadJson("{\"version\":2,\"tags\":{}}", &err));
  TEST_ASSERT_EQUAL_STRING("unsupported version", err.c_str());
}

void test_load_save_roundtrip_and_volume_default_omitted() {
  const char* json =
      "{\"version\":1,\"tags\":{"
      "\"04:A3:7F:12\":{\"name\":\"Dino\",\"uri\":\"media-source://media_source/media/1.mp3\"},"
      "\"AA:BB:CC:DD\":{\"name\":\"Car\",\"uri\":\"x\",\"content_type\":\"playlist\","
      "\"player\":\"media_player.kids\",\"volume\":0.25,\"shuffle\":true}"
      "}}";
  TagRegistry r;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(r.loadJson(json, &err), err.c_str());
  TEST_ASSERT_EQUAL(2, r.size());

  Tag dino;
  TEST_ASSERT_TRUE(r.find("04:A3:7F:12", dino));
  TEST_ASSERT_EQUAL_FLOAT(kVolumeUseDefault, dino.volume);
  TEST_ASSERT_EQUAL_STRING("music", dino.contentType.c_str());
  TEST_ASSERT_EQUAL_STRING("", dino.player.c_str());

  Tag car;
  TEST_ASSERT_TRUE(r.find("AA:BB:CC:DD", car));
  TEST_ASSERT_EQUAL_FLOAT(0.25f, car.volume);
  TEST_ASSERT_TRUE(car.shuffle);
  TEST_ASSERT_EQUAL_STRING("playlist", car.contentType.c_str());

  std::string out = r.toJson();
  TagRegistry again;
  TEST_ASSERT_TRUE(again.loadJson(out, &err));
  TEST_ASSERT_EQUAL(2, again.size());
  // "volume" must be absent for the tag that uses the default.
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, out));
  TEST_ASSERT_TRUE(doc["tags"]["04:A3:7F:12"]["volume"].isNull());
  TEST_ASSERT_EQUAL_FLOAT(0.25f, doc["tags"]["AA:BB:CC:DD"]["volume"].as<float>());
}

void test_load_normalizes_uid_keys() {
  TagRegistry r;
  std::string err;
  TEST_ASSERT_TRUE(r.loadJson("{\"version\":1,\"tags\":{\"04:a3:7f:12\":{\"name\":\"D\",\"uri\":\"u\"}}}", &err));
  Tag t;
  TEST_ASSERT_TRUE(r.find("04:A3:7F:12", t));
  TEST_ASSERT_EQUAL_STRING("04:A3:7F:12", r.all().begin()->first.c_str());
}

void test_load_rejects_invalid_tag_entry() {
  TagRegistry r;
  std::string err;
  TEST_ASSERT_FALSE(r.loadJson("{\"version\":1,\"tags\":{\"04:A3\":{\"name\":\"D\"}}}", &err));
  TEST_ASSERT_EQUAL_STRING("tag 04:A3: uri is required", err.c_str());
  TEST_ASSERT_FALSE(r.loadJson("{\"version\":1,\"tags\":{\"04:A3\":\"nope\"}}", &err));
  TEST_ASSERT_EQUAL_STRING("tag 04:A3: tag must be an object", err.c_str());
}

void test_tag_from_json_rejects_string_volume() {
  JsonDocument doc;
  deserializeJson(doc, "{\"name\":\"D\",\"uri\":\"u\",\"volume\":\"loud\"}");
  Tag t;
  std::string err;
  TEST_ASSERT_FALSE(TagRegistry::tagFromJson(doc.as<JsonVariantConst>(), t, &err));
  TEST_ASSERT_EQUAL_STRING("volume must be a number", err.c_str());
}

void test_tag_from_json_null_volume_means_default() {
  JsonDocument doc;
  deserializeJson(doc, "{\"name\":\"D\",\"uri\":\"u\",\"volume\":null,\"content_type\":\"\"}");
  Tag t;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(TagRegistry::tagFromJson(doc.as<JsonVariantConst>(), t, &err), err.c_str());
  TEST_ASSERT_EQUAL_FLOAT(kVolumeUseDefault, t.volume);
  TEST_ASSERT_EQUAL_STRING("music", t.contentType.c_str());
}

void test_capacity_guard() {
  TagRegistry r;
  Tag t = sampleTag();
  char uid[16];
  for (size_t i = 0; i < TagRegistry::kMaxTags; ++i) {
    snprintf(uid, sizeof(uid), "%02X:%02X", (unsigned)(i / 256), (unsigned)(i % 256));
    TEST_ASSERT_EQUAL(static_cast<int>(TagRegistry::UpsertResult::Ok), static_cast<int>(r.upsert(uid, t)));
  }
  std::string err;
  TEST_ASSERT_EQUAL(static_cast<int>(TagRegistry::UpsertResult::Full),
                    static_cast<int>(r.upsert("FF:FF:FF", t, &err)));
  TEST_ASSERT_EQUAL_STRING("registry is full", err.c_str());
  // Updating an existing tag while full is still allowed.
  TEST_ASSERT_EQUAL(static_cast<int>(TagRegistry::UpsertResult::Ok), static_cast<int>(r.upsert("00:00", t)));
  TEST_ASSERT_EQUAL(TagRegistry::kMaxTags, r.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_registry_serializes);
  RUN_TEST(test_normalize_uid_uppercases_and_strips_whitespace);
  RUN_TEST(test_upsert_then_find_roundtrip);
  RUN_TEST(test_validate_rejects_bad_tags);
  RUN_TEST(test_upsert_invalid_returns_invalid_and_does_not_store);
  RUN_TEST(test_remove);
  RUN_TEST(test_load_parse_error_leaves_registry_unchanged);
  RUN_TEST(test_load_rejects_wrong_version);
  RUN_TEST(test_load_save_roundtrip_and_volume_default_omitted);
  RUN_TEST(test_load_normalizes_uid_keys);
  RUN_TEST(test_load_rejects_invalid_tag_entry);
  RUN_TEST(test_tag_from_json_rejects_string_volume);
  RUN_TEST(test_tag_from_json_null_volume_means_default);
  RUN_TEST(test_capacity_guard);
  return UNITY_END();
}
