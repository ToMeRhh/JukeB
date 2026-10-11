# RFID Jukebox Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP32 + RC522 firmware that, when a tag is placed, tells Home Assistant to play a configured media item on a configured Cast speaker, with all configuration done from a web UI hosted on the ESP32.

**Architecture:** Pure-logic modules (`TagRegistry`, `Settings`, `HaPayload`, `PresenceTracker`) have no Arduino dependency and are unit-tested on the host with PlatformIO's `native` environment. Arduino modules (`TagReader`, `Storage`, `HaClient`, `WifiSetup`, `WebApi`, `Led`) wrap hardware and network and are wired together in `main.cpp`. Web handlers run on the async TCP task, so they only mutate state under a mutex and enqueue network work (play / test / reboot) for `loop()` to perform. Home Assistant receives one JSON webhook and runs one automation.

**Tech Stack:** PlatformIO, pioarduino `espressif32` platform (Arduino core 3.x), `esp32dev` board, LittleFS, MFRC522v2 (OSSLibraries, via git URL), ESPAsyncWebServer 3.12 + AsyncTCP 3.5 (ESP32Async), ArduinoJson 7, Unity for native tests, Home Assistant webhook automation.

**Spec:** `docs/superpowers/specs/2026-10-11-rfid-jukebox-design.md`

## Global Constraints

- Firmware lives in `firmware/` as a PlatformIO project; the repo root holds `README.md`, `CLAUDE.md`, `docs/`, `homeassistant/`.
- Pins: SS=5, SCK=18, MOSI=23, MISO=19, RST=21 (driven HIGH), onboard LED=2.
- `TagRegistry`, `Settings`, `HaPayload`, `PresenceTracker` must not include any Arduino or ESP-IDF header. ArduinoJson and the C++ standard library only.
- UID string format: upper-case hex bytes joined by `:` (e.g. `04:A3:7F:12`).
- Tag fields: `name`, `uri`, `content_type` (default `music`), `player` (empty = default), `volume` (absent/null = use default, `-1` = don't touch, else 0.0–1.0), `shuffle` (default false). Max 200 tags.
- Settings: `on_remove` ∈ {`none`,`pause`,`stop`} (invalid → `none`), `pause_grace_ms` clamped 200–30000, `default_volume` clamped 0.0–1.0, `ha_url` normalized (trailing slash stripped, `http://` added if no scheme), masked password `********` on read means "unchanged" on write.
- Webhook URL: `<ha_url>/api/webhook/<webhook_id>`, POST, `Content-Type: application/json`, 5 s timeout, one retry after 500 ms.
- Payload: `{"event":"play","uid":..,"name":..,"uri":..,"content_type":..,"player":..,"volume":..,"shuffle":..}`; `pause`/`stop` carry only `event`,`uid`,`player`; `test` carries only `event`.
- Reader: poll every 100 ms using WUPA; placed after 2 consecutive sightings; removed after `pause_grace_ms` without sighting.
- AP mode SSID `Jukebox-Setup`, open, captive portal; STA mode uses mDNS `<hostname>.local`.
- Web API bodies limited to 8 KB; validation errors → 400 `{"error":"..."}`; unknown UID → 404; registry full → 507.
- All commits end with the attribution lines from the session (Co-Authored-By + Claude-Session).

## Review Focus

1. A `tags.json` written by hand with lower-case UID keys must load and still match the reader's upper-case UIDs → test in Task 2 (`test_load_normalizes_uid_keys`).
2. A tag body with `"volume":"loud"` (string) must be rejected with a 400, not crash the parser → test in Task 2 (`test_tag_from_json_rejects_string_volume`).
3. `pause_grace_ms` sent as a string or negative number must leave the current value untouched → test in Task 3 (`test_apply_ignores_bad_grace_type`).
4. Lifting the object briefly (shorter than the grace period) and putting it back must produce neither a removal nor a second play → test in Task 5 (`test_brief_lift_within_grace_is_ignored`).
5. Placing the same tag again after a real removal must play again → test in Task 5 (`test_replace_after_removal_places_again`).

---

### Task 1: Repository scaffold, PlatformIO project, README and CLAUDE.md

**Files:**
- Create: `.gitignore`
- Create: `README.md`
- Create: `CLAUDE.md`
- Create: `firmware/platformio.ini`
- Create: `firmware/src/main.cpp` (stub, replaced in Task 9)
- Create: `firmware/test/test_smoke/test_main.cpp`
- Create: `firmware/data/.gitkeep`

**Interfaces:**
- Consumes: nothing.
- Produces: a `firmware/` PlatformIO project with two environments, `esp32dev` (board build) and `native` (host unit tests). Later tasks add `.cpp` files to the native `build_src_filter`.

- [ ] **Step 1: Create `.gitignore`**

```gitignore
# PlatformIO
firmware/.pio/
firmware/.vscode/
# OS / editor
.DS_Store
*.swp
# Python
__pycache__/
```

- [ ] **Step 2: Create `firmware/platformio.ini`**

```ini
[platformio]
default_envs = esp32dev

[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.38/platform-espressif32.zip
board = esp32dev
framework = arduino
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
board_build.filesystem = littlefs
board_build.partitions = min_spiffs.csv
build_flags =
  -DJUKEBOX_VERSION=\"0.1.0\"
lib_deps =
  https://github.com/OSSLibraries/Arduino_MFRC522v2.git
  esp32async/ESPAsyncWebServer@^3.12
  esp32async/AsyncTCP@^3.5
  bblanchon/ArduinoJson@^7.4

[env:native]
platform = native
test_framework = unity
build_flags =
  -std=gnu++17
  -DNATIVE_TEST
lib_deps =
  bblanchon/ArduinoJson@^7.4
build_src_filter = -<*>
```

Note: `build_src_filter = -<*>` excludes every `src/` file from the native build for now. Tasks 2–5 each add their pure-logic `.cpp` to it.

- [ ] **Step 3: Create the stub `firmware/src/main.cpp`**

```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial.println("jukebox stub");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 4: Create `firmware/data/.gitkeep`** (empty file) so the LittleFS data folder exists in git.

- [ ] **Step 5: Write the smoke test `firmware/test/test_smoke/test_main.cpp`**

```cpp
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
```

- [ ] **Step 6: Run the native test and the board build**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native`
Expected: `1 test cases: 1 succeeded`.

Run (from `firmware/`): `~/.platformio/penv/bin/pio run -e esp32dev`
Expected: `[SUCCESS]`. The first run downloads the libraries.

- [ ] **Step 7: Write the top-level `README.md`**

````markdown
# Jukebox

A music box for physical objects. Put an object with an RFID tag on the box and a
song plays on a Google Home / Chromecast speaker. Songs, speakers and behaviour are
configured from a web page served by the box itself.

```
[RC522] --SPI--> [ESP32 firmware] --HTTP POST JSON--> [Home Assistant webhook automation] --play_media--> [Cast speaker]
                       ^
                       | browser (LAN)
                  [Web UI on ESP32]
```

## Repository layout

| Folder | What |
|--------|------|
| `firmware/` | PlatformIO project for the ESP32 (C++). See `firmware/README.md`. |
| `homeassistant/` | The Home Assistant automation that turns webhook calls into playback. |
| `docs/` | Design specs and implementation plans. |

## Hardware

ESP32 dev board + RFID-RC522 on VSPI:

| RC522 | ESP32 GPIO |
|-------|-----------|
| SDA/SS | 5 |
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |
| RST | 21 |
| 3.3V / GND | 3.3V / GND |

## Quick start

1. Flash the firmware and the web UI: see `firmware/README.md`.
2. Join the `Jukebox-Setup` WiFi network, enter your WiFi credentials in the portal.
3. Install the automation from `homeassistant/jukebox_automation.yaml`.
4. Open `http://jukebox.local`, set the Home Assistant URL and your speaker entity IDs.
5. Scan an object, give it a name and a media URI, save. Scan it again to play.

Media URIs are whatever your Home Assistant media players accept, e.g.
`media-source://media_source/media/1.mp3` for files stored on Home Assistant.
````

- [ ] **Step 8: Write `CLAUDE.md`**

```markdown
# CLAUDE.md

Guidance for AI-assisted work in this repository.

## What this is

An RFID music box: ESP32 + RC522 reader. A scanned tag makes the ESP32 POST a JSON
payload to a Home Assistant webhook; a Home Assistant automation plays the media on a
Cast speaker. Configuration lives on the ESP32 and is edited through its web UI.

Design spec: `docs/superpowers/specs/2026-10-11-rfid-jukebox-design.md`.
Implementation plan: `docs/superpowers/plans/2026-10-11-rfid-jukebox.md`.

## Layout

- `firmware/` PlatformIO project. `src/` firmware, `data/` files uploaded to LittleFS
  (the web UI), `test/` native unit tests.
- `homeassistant/` the webhook automation YAML and its README.
- `docs/` specs and plans.

Keep new top-level concerns in their own folder (e.g. `tools/`).

## Build and test

All commands run from `firmware/`. PlatformIO lives at `~/.platformio/penv/bin/pio`.

- `pio test -e native` runs the host unit tests (no board needed). Run this after every change to a pure-logic module.
- `pio run -e esp32dev` compiles the firmware.
- `pio run -e esp32dev -t buildfs` builds the LittleFS image from `data/`.
- `pio run -e esp32dev -t upload` / `-t uploadfs` flash firmware / filesystem.
- `pio device monitor` serial log at 115200.

## Conventions

- Pure-logic modules (`TagRegistry`, `Settings`, `HaPayload`, `PresenceTracker`) must
  not include Arduino or ESP-IDF headers. They are compiled for the host by the
  `native` environment (`build_src_filter` in `platformio.ini`) and unit-tested with
  Unity. Put new logic there first, test it there, then wire it into Arduino code.
- Arduino modules (`TagReader`, `Storage`, `HaClient`, `WifiSetup`, `WebApi`, `Led`)
  wrap hardware and network. `main.cpp` only wires modules together.
- Web handlers run on the async TCP task. They must not block (no HTTP calls, no
  delays). Mutate `AppState` under `state.mutex`; enqueue network work as a `Command`
  for `loop()`.
- JSON everywhere uses ArduinoJson 7 (`JsonDocument`). Keys are snake_case.
- UID strings are upper-case hex bytes joined with `:`.
- Tests: TDD. Write the failing Unity test, watch it fail, implement, watch it pass.

## Hardware pins

SS=5, SCK=18, MOSI=23, MISO=19 (VSPI defaults), RST=21 (held HIGH, software reset
only), onboard LED=2.

## Home Assistant contract

POST `<ha_url>/api/webhook/<webhook_id>` with JSON:
`{"event":"play"|"pause"|"stop"|"test", "uid", "name", "uri", "content_type", "player", "volume"?, "shuffle"}`.
Changing what Home Assistant does with it (e.g. moving to Music Assistant) is an
automation change, never a firmware change.
```

- [ ] **Step 9: Commit**

```bash
git add .gitignore README.md CLAUDE.md firmware/
git commit -m "Scaffold PlatformIO project, README and CLAUDE.md"
```

---

### Task 2: TagRegistry (pure logic)

**Files:**
- Create: `firmware/src/TagRegistry.h`
- Create: `firmware/src/TagRegistry.cpp`
- Create: `firmware/test/test_registry/test_main.cpp`
- Modify: `firmware/platformio.ini` (native `build_src_filter`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `constexpr float kVolumeUseDefault = -2.0f; constexpr float kVolumeNoChange = -1.0f;`
  - `struct Tag { std::string name, uri, contentType="music", player; float volume=kVolumeUseDefault; bool shuffle=false; }`
  - `class TagRegistry` with `bool loadJson(const std::string&, std::string* err=nullptr)`, `std::string toJson() const`, `bool find(const std::string& uid, Tag& out) const`, `UpsertResult upsert(const std::string& uid, const Tag&, std::string* err=nullptr)` (`enum class UpsertResult { Ok, Invalid, Full }`), `bool remove(const std::string& uid)`, `const std::map<std::string, Tag>& all() const`, `size_t size() const`, `static std::string normalizeUid(const std::string&)`, `static bool validate(const Tag&, std::string* err)`, `static bool tagFromJson(JsonVariantConst, Tag& out, std::string* err)`, `static void tagToJson(const Tag&, JsonObject out)`, `static constexpr size_t kMaxTags = 200`.

- [ ] **Step 1: Write the failing tests `firmware/test/test_registry/test_main.cpp`**

```cpp
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
```

- [ ] **Step 2: Add the source to the native build**

In `firmware/platformio.ini`, change the native env line to:

```ini
build_src_filter = +<TagRegistry.cpp>
```

- [ ] **Step 3: Run the test to verify it fails**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native -f test_registry`
Expected: compile error, `TagRegistry.h: No such file or directory`.

- [ ] **Step 4: Write `firmware/src/TagRegistry.h`**

```cpp
#pragma once

#include <ArduinoJson.h>
#include <map>
#include <string>

constexpr float kVolumeUseDefault = -2.0f;  // not stored; use Settings::defaultVolume
constexpr float kVolumeNoChange = -1.0f;    // stored as -1; do not send volume to HA

struct Tag {
  std::string name;
  std::string uri;
  std::string contentType = "music";
  std::string player;  // empty = use Settings::defaultPlayer
  float volume = kVolumeUseDefault;
  bool shuffle = false;
};

class TagRegistry {
 public:
  static constexpr size_t kMaxTags = 200;
  static constexpr int kVersion = 1;
  enum class UpsertResult { Ok, Invalid, Full };

  // Replaces the contents with the file's tags. On any error returns false,
  // sets *err and leaves the registry unchanged.
  bool loadJson(const std::string& json, std::string* err = nullptr);
  std::string toJson() const;

  bool find(const std::string& uid, Tag& out) const;
  UpsertResult upsert(const std::string& uid, const Tag& tag, std::string* err = nullptr);
  bool remove(const std::string& uid);
  const std::map<std::string, Tag>& all() const { return tags_; }
  size_t size() const { return tags_.size(); }

  static std::string normalizeUid(const std::string& uid);
  static bool validate(const Tag& tag, std::string* err);
  static bool tagFromJson(JsonVariantConst v, Tag& out, std::string* err);
  static void tagToJson(const Tag& tag, JsonObject out);

 private:
  std::map<std::string, Tag> tags_;
};
```

- [ ] **Step 5: Write `firmware/src/TagRegistry.cpp`**

```cpp
#include "TagRegistry.h"

#include <cctype>
#include <utility>

static void setErr(std::string* err, const std::string& msg) {
  if (err) *err = msg;
}

std::string TagRegistry::normalizeUid(const std::string& uid) {
  std::string out;
  for (char c : uid) {
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

bool TagRegistry::validate(const Tag& t, std::string* err) {
  if (t.name.empty()) { setErr(err, "name is required"); return false; }
  if (t.uri.empty()) { setErr(err, "uri is required"); return false; }
  if (t.contentType.empty()) { setErr(err, "content_type is required"); return false; }
  const bool volumeOk = t.volume == kVolumeUseDefault || t.volume == kVolumeNoChange ||
                        (t.volume >= 0.0f && t.volume <= 1.0f);
  if (!volumeOk) { setErr(err, "volume must be between 0 and 1, or -1"); return false; }
  return true;
}

bool TagRegistry::tagFromJson(JsonVariantConst v, Tag& out, std::string* err) {
  if (!v.is<JsonObjectConst>()) { setErr(err, "tag must be an object"); return false; }
  Tag t;
  t.name = v["name"] | "";
  t.uri = v["uri"] | "";
  t.contentType = v["content_type"] | "music";
  if (t.contentType.empty()) t.contentType = "music";
  t.player = v["player"] | "";
  JsonVariantConst vol = v["volume"];
  if (vol.isNull()) {
    t.volume = kVolumeUseDefault;
  } else if (vol.is<float>()) {
    t.volume = vol.as<float>();
  } else {
    setErr(err, "volume must be a number");
    return false;
  }
  t.shuffle = v["shuffle"] | false;
  if (!validate(t, err)) return false;
  out = t;
  return true;
}

void TagRegistry::tagToJson(const Tag& t, JsonObject o) {
  o["name"] = t.name;
  o["uri"] = t.uri;
  o["content_type"] = t.contentType;
  o["player"] = t.player;
  if (t.volume != kVolumeUseDefault) o["volume"] = t.volume;
  o["shuffle"] = t.shuffle;
}

bool TagRegistry::loadJson(const std::string& json, std::string* err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { setErr(err, std::string("parse error: ") + e.c_str()); return false; }
  if (!doc.is<JsonObjectConst>()) { setErr(err, "root must be an object"); return false; }
  const int version = doc["version"] | 0;
  if (version != kVersion) { setErr(err, "unsupported version"); return false; }

  std::map<std::string, Tag> loaded;
  for (JsonPairConst kv : doc["tags"].as<JsonObjectConst>()) {
    Tag t;
    std::string terr;
    if (!tagFromJson(kv.value(), t, &terr)) {
      setErr(err, std::string("tag ") + kv.key().c_str() + ": " + terr);
      return false;
    }
    const std::string uid = normalizeUid(kv.key().c_str());
    if (uid.empty()) { setErr(err, "empty uid"); return false; }
    if (loaded.size() >= kMaxTags) { setErr(err, "too many tags"); return false; }
    loaded[uid] = t;
  }
  tags_ = std::move(loaded);
  return true;
}

std::string TagRegistry::toJson() const {
  JsonDocument doc;
  doc["version"] = kVersion;
  JsonObject tags = doc["tags"].to<JsonObject>();
  for (const auto& kv : tags_) {
    JsonObject o = tags[kv.first].to<JsonObject>();
    tagToJson(kv.second, o);
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool TagRegistry::find(const std::string& uid, Tag& out) const {
  auto it = tags_.find(normalizeUid(uid));
  if (it == tags_.end()) return false;
  out = it->second;
  return true;
}

TagRegistry::UpsertResult TagRegistry::upsert(const std::string& rawUid, const Tag& tag, std::string* err) {
  const std::string uid = normalizeUid(rawUid);
  if (uid.empty()) { setErr(err, "uid is required"); return UpsertResult::Invalid; }
  if (!validate(tag, err)) return UpsertResult::Invalid;
  if (tags_.find(uid) == tags_.end() && tags_.size() >= kMaxTags) {
    setErr(err, "registry is full");
    return UpsertResult::Full;
  }
  tags_[uid] = tag;
  return UpsertResult::Ok;
}

bool TagRegistry::remove(const std::string& rawUid) {
  return tags_.erase(normalizeUid(rawUid)) > 0;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native -f test_registry`
Expected: `14 test cases: 14 succeeded`.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/TagRegistry.h firmware/src/TagRegistry.cpp firmware/test/test_registry firmware/platformio.ini
git commit -m "Add TagRegistry with native unit tests"
```

---

### Task 3: Settings (pure logic)

**Files:**
- Create: `firmware/src/Settings.h`
- Create: `firmware/src/Settings.cpp`
- Create: `firmware/test/test_settings/test_main.cpp`
- Modify: `firmware/platformio.ini` (native `build_src_filter`)

**Interfaces:**
- Consumes: nothing.
- Produces: `struct Settings` with public fields `wifiSsid, wifiPass, hostname("jukebox"), haUrl("http://homeassistant.local:8123"), webhookId("jukebox"), std::vector<std::string> players, defaultPlayer, float defaultVolume(0.3f), enum class OnRemove {None, Pause, Stop} onRemove(None), uint32_t pauseGraceMs(1500)`; methods `bool loadJson(const std::string&, std::string* err=nullptr)`, `bool applyJson(const std::string&, std::string* err=nullptr)`, `std::string toJson(bool maskPassword) const`, `std::string webhookUrl() const`, `static std::string normalizeUrl(const std::string&)`, `static const char* onRemoveToString(OnRemove)`, `static bool onRemoveFromString(const std::string&, OnRemove& out)`, `static constexpr const char* kMaskedPassword = "********"`.

- [ ] **Step 1: Write the failing tests `firmware/test/test_settings/test_main.cpp`**

```cpp
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
```

- [ ] **Step 2: Add the source to the native build**

In `firmware/platformio.ini`, native env:

```ini
build_src_filter = +<TagRegistry.cpp> +<Settings.cpp>
```

- [ ] **Step 3: Run the test to verify it fails**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native -f test_settings`
Expected: compile error, `Settings.h: No such file or directory`.

- [ ] **Step 4: Write `firmware/src/Settings.h`**

```cpp
#pragma once

#include <ArduinoJson.h>
#include <cstdint>
#include <string>
#include <vector>

struct Settings {
  static constexpr int kVersion = 1;
  static constexpr const char* kMaskedPassword = "********";
  enum class OnRemove { None, Pause, Stop };

  std::string wifiSsid;
  std::string wifiPass;
  std::string hostname = "jukebox";
  std::string haUrl = "http://homeassistant.local:8123";
  std::string webhookId = "jukebox";
  std::vector<std::string> players;
  std::string defaultPlayer;
  float defaultVolume = 0.3f;
  OnRemove onRemove = OnRemove::None;
  uint32_t pauseGraceMs = 1500;

  // Parse a whole file: starts from defaults, applies every field present.
  // On error returns false, sets *err and leaves *this unchanged.
  bool loadJson(const std::string& json, std::string* err = nullptr);
  // Apply a partial update (API body) on top of the current values.
  // A masked password keeps the current one. Same error contract as loadJson.
  bool applyJson(const std::string& json, std::string* err = nullptr);
  std::string toJson(bool maskPassword) const;

  std::string webhookUrl() const;

  static std::string normalizeUrl(const std::string& url);
  static const char* onRemoveToString(OnRemove v);
  static bool onRemoveFromString(const std::string& s, OnRemove& out);

 private:
  bool applyObject(JsonObjectConst o, std::string* err);
};
```

- [ ] **Step 5: Write `firmware/src/Settings.cpp`**

```cpp
#include "Settings.h"

#include <algorithm>

static std::string trim(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

static void setErr(std::string* err, const std::string& msg) {
  if (err) *err = msg;
}

std::string Settings::normalizeUrl(const std::string& url) {
  std::string u = trim(url);
  while (!u.empty() && u.back() == '/') u.pop_back();
  if (u.empty()) return u;
  if (u.find("://") == std::string::npos) u = "http://" + u;
  return u;
}

const char* Settings::onRemoveToString(OnRemove v) {
  switch (v) {
    case OnRemove::Pause: return "pause";
    case OnRemove::Stop: return "stop";
    default: return "none";
  }
}

bool Settings::onRemoveFromString(const std::string& s, OnRemove& out) {
  if (s == "none") { out = OnRemove::None; return true; }
  if (s == "pause") { out = OnRemove::Pause; return true; }
  if (s == "stop") { out = OnRemove::Stop; return true; }
  return false;
}

std::string Settings::webhookUrl() const {
  return haUrl + "/api/webhook/" + webhookId;
}

bool Settings::applyObject(JsonObjectConst o, std::string* err) {
  Settings next = *this;

  if (o["wifi_ssid"].is<const char*>()) next.wifiSsid = trim(o["wifi_ssid"].as<const char*>());
  if (o["wifi_pass"].is<const char*>()) {
    const std::string p = o["wifi_pass"].as<const char*>();
    if (p != kMaskedPassword) next.wifiPass = p;
  }
  if (o["hostname"].is<const char*>()) {
    next.hostname = trim(o["hostname"].as<const char*>());
    if (next.hostname.empty()) next.hostname = "jukebox";
  }
  if (o["ha_url"].is<const char*>()) next.haUrl = normalizeUrl(o["ha_url"].as<const char*>());
  if (o["webhook_id"].is<const char*>()) next.webhookId = trim(o["webhook_id"].as<const char*>());
  if (o["players"].is<JsonArrayConst>()) {
    next.players.clear();
    for (JsonVariantConst v : o["players"].as<JsonArrayConst>()) {
      if (!v.is<const char*>()) continue;
      const std::string p = trim(v.as<const char*>());
      if (p.empty()) continue;
      if (std::find(next.players.begin(), next.players.end(), p) != next.players.end()) continue;
      next.players.push_back(p);
    }
  }
  if (o["default_player"].is<const char*>()) next.defaultPlayer = trim(o["default_player"].as<const char*>());
  if (o["default_volume"].is<float>()) {
    next.defaultVolume = std::min(1.0f, std::max(0.0f, o["default_volume"].as<float>()));
  }
  if (o["on_remove"].is<const char*>()) {
    OnRemove v;
    next.onRemove = onRemoveFromString(o["on_remove"].as<const char*>(), v) ? v : OnRemove::None;
  }
  if (o["pause_grace_ms"].is<uint32_t>()) {
    next.pauseGraceMs = std::min<uint32_t>(30000, std::max<uint32_t>(200, o["pause_grace_ms"].as<uint32_t>()));
  }

  if (next.haUrl.empty()) { setErr(err, "ha_url is required"); return false; }
  if (next.webhookId.empty()) { setErr(err, "webhook_id is required"); return false; }
  if (!next.defaultPlayer.empty() &&
      std::find(next.players.begin(), next.players.end(), next.defaultPlayer) == next.players.end()) {
    setErr(err, "default_player must be one of players");
    return false;
  }

  *this = next;
  return true;
}

static bool parseObject(const std::string& json, JsonDocument& doc, std::string* err) {
  const DeserializationError e = deserializeJson(doc, json);
  if (e) { setErr(err, std::string("parse error: ") + e.c_str()); return false; }
  if (!doc.is<JsonObjectConst>()) { setErr(err, "root must be an object"); return false; }
  return true;
}

bool Settings::loadJson(const std::string& json, std::string* err) {
  JsonDocument doc;
  if (!parseObject(json, doc, err)) return false;
  Settings fresh;
  if (!fresh.applyObject(doc.as<JsonObjectConst>(), err)) return false;
  *this = fresh;
  return true;
}

bool Settings::applyJson(const std::string& json, std::string* err) {
  JsonDocument doc;
  if (!parseObject(json, doc, err)) return false;
  return applyObject(doc.as<JsonObjectConst>(), err);
}

std::string Settings::toJson(bool maskPassword) const {
  JsonDocument doc;
  doc["version"] = kVersion;
  doc["wifi_ssid"] = wifiSsid;
  doc["wifi_pass"] = (maskPassword && !wifiPass.empty()) ? std::string(kMaskedPassword) : wifiPass;
  doc["hostname"] = hostname;
  doc["ha_url"] = haUrl;
  doc["webhook_id"] = webhookId;
  JsonArray arr = doc["players"].to<JsonArray>();
  for (const auto& p : players) arr.add(p);
  doc["default_player"] = defaultPlayer;
  doc["default_volume"] = defaultVolume;
  doc["on_remove"] = onRemoveToString(onRemove);
  doc["pause_grace_ms"] = pauseGraceMs;
  std::string out;
  serializeJson(doc, out);
  return out;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native -f test_settings`
Expected: `14 test cases: 14 succeeded`.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/Settings.h firmware/src/Settings.cpp firmware/test/test_settings firmware/platformio.ini
git commit -m "Add Settings with native unit tests"
```

---

### Task 4: HaPayload (pure logic)

**Files:**
- Create: `firmware/src/HaPayload.h`
- Create: `firmware/src/HaPayload.cpp`
- Create: `firmware/test/test_hapayload/test_main.cpp`
- Modify: `firmware/platformio.ini` (native `build_src_filter`)

**Interfaces:**
- Consumes: `Tag`, `kVolumeUseDefault`, `kVolumeNoChange` (Task 2); `Settings` (Task 3).
- Produces: namespace `HaPayload` with `std::string effectivePlayer(const Tag&, const Settings&)`, `float effectiveVolume(const Tag&, const Settings&)` (returns `kVolumeNoChange` when volume must not be sent), `std::string play(const std::string& uid, const Tag&, const Settings&)`, `std::string control(const std::string& event, const std::string& uid, const Tag&, const Settings&)`, `std::string test()`.

- [ ] **Step 1: Write the failing tests `firmware/test/test_hapayload/test_main.cpp`**

```cpp
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_effective_player_falls_back_to_default);
  RUN_TEST(test_effective_volume);
  RUN_TEST(test_play_payload_with_default_volume_and_player);
  RUN_TEST(test_play_payload_omits_volume_when_no_change);
  RUN_TEST(test_control_payloads);
  RUN_TEST(test_test_payload);
  return UNITY_END();
}
```

- [ ] **Step 2: Add the source to the native build**

```ini
build_src_filter = +<TagRegistry.cpp> +<Settings.cpp> +<HaPayload.cpp>
```

- [ ] **Step 3: Run the test to verify it fails**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native -f test_hapayload`
Expected: compile error, `HaPayload.h: No such file or directory`.

- [ ] **Step 4: Write `firmware/src/HaPayload.h`**

```cpp
#pragma once

#include <string>

#include "Settings.h"
#include "TagRegistry.h"

// Builds the JSON bodies sent to the Home Assistant webhook. No I/O here.
namespace HaPayload {

std::string effectivePlayer(const Tag& tag, const Settings& s);
// Returns kVolumeNoChange when no volume must be sent.
float effectiveVolume(const Tag& tag, const Settings& s);

std::string play(const std::string& uid, const Tag& tag, const Settings& s);
// event is "pause" or "stop".
std::string control(const std::string& event, const std::string& uid, const Tag& tag, const Settings& s);
std::string test();

}  // namespace HaPayload
```

- [ ] **Step 5: Write `firmware/src/HaPayload.cpp`**

```cpp
#include "HaPayload.h"

#include <ArduinoJson.h>

namespace HaPayload {

std::string effectivePlayer(const Tag& tag, const Settings& s) {
  return tag.player.empty() ? s.defaultPlayer : tag.player;
}

float effectiveVolume(const Tag& tag, const Settings& s) {
  if (tag.volume == kVolumeUseDefault) return s.defaultVolume;
  return tag.volume;  // 0..1 or kVolumeNoChange
}

static std::string serialize(const JsonDocument& doc) {
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string play(const std::string& uid, const Tag& tag, const Settings& s) {
  JsonDocument doc;
  doc["event"] = "play";
  doc["uid"] = uid;
  doc["name"] = tag.name;
  doc["uri"] = tag.uri;
  doc["content_type"] = tag.contentType;
  doc["player"] = effectivePlayer(tag, s);
  const float volume = effectiveVolume(tag, s);
  if (volume != kVolumeNoChange) doc["volume"] = volume;
  doc["shuffle"] = tag.shuffle;
  return serialize(doc);
}

std::string control(const std::string& event, const std::string& uid, const Tag& tag, const Settings& s) {
  JsonDocument doc;
  doc["event"] = event;
  doc["uid"] = uid;
  doc["player"] = effectivePlayer(tag, s);
  return serialize(doc);
}

std::string test() {
  JsonDocument doc;
  doc["event"] = "test";
  return serialize(doc);
}

}  // namespace HaPayload
```

- [ ] **Step 6: Run the tests to verify they pass**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native -f test_hapayload`
Expected: `6 test cases: 6 succeeded`.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/HaPayload.h firmware/src/HaPayload.cpp firmware/test/test_hapayload firmware/platformio.ini
git commit -m "Add HaPayload builder with native unit tests"
```

---

### Task 5: PresenceTracker (pure logic)

**Files:**
- Create: `firmware/src/PresenceTracker.h`
- Create: `firmware/src/PresenceTracker.cpp`
- Create: `firmware/test/test_presence/test_main.cpp`
- Modify: `firmware/platformio.ini` (native `build_src_filter`)

**Interfaces:**
- Consumes: nothing.
- Produces: `class PresenceTracker` with `struct Result { bool removed=false; std::string removedUid; bool placed=false; std::string placedUid; }`, `void setGraceMs(uint32_t)`, `void setRequiredHits(uint8_t)` (default 2), `Result update(bool seen, const std::string& uid, uint32_t nowMs)`, `bool present() const`, `const std::string& currentUid() const`.

- [ ] **Step 1: Write the failing tests `firmware/test/test_presence/test_main.cpp`**

```cpp
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

int main() {
  UNITY_BEGIN();
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
```

- [ ] **Step 2: Add the source to the native build**

```ini
build_src_filter = +<TagRegistry.cpp> +<Settings.cpp> +<HaPayload.cpp> +<PresenceTracker.cpp>
```

- [ ] **Step 3: Run the test to verify it fails**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native -f test_presence`
Expected: compile error, `PresenceTracker.h: No such file or directory`.

- [ ] **Step 4: Write `firmware/src/PresenceTracker.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>

// Turns a stream of "did we see a tag this poll, and which" observations into
// placed/removed events with debounce (consecutive hits) and a removal grace period.
class PresenceTracker {
 public:
  struct Result {
    bool removed = false;
    std::string removedUid;
    bool placed = false;
    std::string placedUid;
  };

  void setGraceMs(uint32_t ms) { graceMs_ = ms; }
  void setRequiredHits(uint8_t hits) { requiredHits_ = hits == 0 ? 1 : hits; }

  Result update(bool seen, const std::string& uid, uint32_t nowMs);

  bool present() const { return present_; }
  const std::string& currentUid() const { return currentUid_; }

 private:
  uint32_t graceMs_ = 1500;
  uint8_t requiredHits_ = 2;

  bool present_ = false;
  std::string currentUid_;
  uint32_t lastSeenMs_ = 0;

  std::string candidateUid_;
  uint8_t candidateHits_ = 0;
};
```

- [ ] **Step 5: Write `firmware/src/PresenceTracker.cpp`**

```cpp
#include "PresenceTracker.h"

PresenceTracker::Result PresenceTracker::update(bool seen, const std::string& uid, uint32_t nowMs) {
  Result r;

  if (seen && present_ && uid == currentUid_) {
    lastSeenMs_ = nowMs;
    candidateHits_ = 0;
    candidateUid_.clear();
    return r;
  }

  if (seen) {
    if (uid == candidateUid_) {
      ++candidateHits_;
    } else {
      candidateUid_ = uid;
      candidateHits_ = 1;
    }
    if (candidateHits_ >= requiredHits_) {
      if (present_) {
        r.removed = true;
        r.removedUid = currentUid_;
      }
      present_ = true;
      currentUid_ = candidateUid_;
      lastSeenMs_ = nowMs;
      candidateHits_ = 0;
      candidateUid_.clear();
      r.placed = true;
      r.placedUid = currentUid_;
    }
    return r;
  }

  // Not seen.
  candidateHits_ = 0;
  candidateUid_.clear();
  if (present_ && (nowMs - lastSeenMs_) >= graceMs_) {
    r.removed = true;
    r.removedUid = currentUid_;
    present_ = false;
    currentUid_.clear();
  }
  return r;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native`
Expected: all five suites pass (`test_smoke`, `test_registry`, `test_settings`, `test_hapayload`, `test_presence`).

- [ ] **Step 7: Commit**

```bash
git add firmware/src/PresenceTracker.h firmware/src/PresenceTracker.cpp firmware/test/test_presence firmware/platformio.ini
git commit -m "Add PresenceTracker with native unit tests"
```

---

### Task 6: Arduino support modules: AppState, Storage, Led, HaClient

**Files:**
- Create: `firmware/src/AppState.h`
- Create: `firmware/src/Storage.h`, `firmware/src/Storage.cpp`
- Create: `firmware/src/Led.h`, `firmware/src/Led.cpp`
- Create: `firmware/src/HaClient.h`, `firmware/src/HaClient.cpp`

**Interfaces:**
- Consumes: `TagRegistry` (Task 2), `Settings` (Task 3).
- Produces:
  - `struct HaResult { bool any=false; bool ok=false; int httpCode=0; std::string message; uint32_t atMillis=0; }`
  - `struct Command { enum class Type { PlayTag, TestWebhook, Reboot, SettingsChanged }; Type type; std::string uid; }`
  - `struct AppState { TagRegistry registry; Settings settings; std::string lastScannedUid; uint32_t lastScannedAt; bool lastScannedKnown; HaResult lastResult; std::string warning; bool readerFound; uint8_t readerVersion; std::deque<Command> commands; std::mutex mutex; }`
  - `class Storage { bool begin(); bool loadRegistry(TagRegistry&, std::string& warning); bool saveRegistry(const TagRegistry&); bool loadSettings(Settings&, std::string& warning); bool saveSettings(const Settings&); }`
  - `class Led { explicit Led(uint8_t pin); void begin(); void setBase(bool on); void blink(uint8_t count, uint16_t onMs=80, uint16_t offMs=120); void loop(uint32_t now); }`
  - `namespace HaClient { HaResult send(const std::string& url, const std::string& payload); }`

These modules have no host tests (they need the Arduino core). The verification step is the board build.

- [ ] **Step 1: Write `firmware/src/AppState.h`**

```cpp
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "Settings.h"
#include "TagRegistry.h"

struct HaResult {
  bool any = false;  // false until the first send
  bool ok = false;
  int httpCode = 0;
  std::string message;
  uint32_t atMillis = 0;
};

// Work the web task asks loop() to do (anything that blocks or touches the network).
struct Command {
  enum class Type { PlayTag, TestWebhook, Reboot, SettingsChanged };
  Type type;
  std::string uid;
};

struct AppState {
  TagRegistry registry;
  Settings settings;

  std::string lastScannedUid;
  uint32_t lastScannedAt = 0;
  bool lastScannedKnown = false;

  HaResult lastResult;
  std::string warning;  // config problems to show in the UI
  bool readerFound = false;
  uint8_t readerVersion = 0;

  std::deque<Command> commands;
  std::mutex mutex;  // guards every field above
};
```

- [ ] **Step 2: Write `firmware/src/Storage.h`**

```cpp
#pragma once

#include <string>

#include "Settings.h"
#include "TagRegistry.h"

// LittleFS persistence for tags.json and settings.json.
// A corrupt file is renamed to <name>.bad and defaults are used.
class Storage {
 public:
  static constexpr const char* kTagsPath = "/tags.json";
  static constexpr const char* kSettingsPath = "/settings.json";

  bool begin();
  // Return true when the file was loaded. False means defaults are in use;
  // `warning` is set only if the file existed and was unreadable.
  bool loadRegistry(TagRegistry& registry, std::string& warning);
  bool saveRegistry(const TagRegistry& registry);
  bool loadSettings(Settings& settings, std::string& warning);
  bool saveSettings(const Settings& settings);

 private:
  bool readFile(const char* path, std::string& out);
  bool writeFile(const char* path, const std::string& data);
  void quarantine(const char* path);
};
```

- [ ] **Step 3: Write `firmware/src/Storage.cpp`**

```cpp
#include "Storage.h"

#include <Arduino.h>
#include <LittleFS.h>

bool Storage::begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[storage] LittleFS mount failed");
    return false;
  }
  return true;
}

bool Storage::readFile(const char* path, std::string& out) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out.clear();
  out.reserve(f.size());
  uint8_t buf[256];
  while (f.available()) {
    const size_t n = f.read(buf, sizeof(buf));
    out.append(reinterpret_cast<const char*>(buf), n);
  }
  f.close();
  return true;
}

bool Storage::writeFile(const char* path, const std::string& data) {
  const String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  f.close();
  if (written != data.size()) {
    LittleFS.remove(tmp);
    return false;
  }
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

void Storage::quarantine(const char* path) {
  const String bad = String(path) + ".bad";
  LittleFS.remove(bad);
  LittleFS.rename(path, bad);
  Serial.printf("[storage] %s is corrupt, moved to %s\n", path, bad.c_str());
}

bool Storage::loadRegistry(TagRegistry& registry, std::string& warning) {
  std::string json;
  if (!readFile(kTagsPath, json)) return false;
  std::string err;
  if (registry.loadJson(json, &err)) return true;
  warning = std::string("tags.json was corrupt (") + err + "); started with no tags";
  quarantine(kTagsPath);
  return false;
}

bool Storage::saveRegistry(const TagRegistry& registry) {
  return writeFile(kTagsPath, registry.toJson());
}

bool Storage::loadSettings(Settings& settings, std::string& warning) {
  std::string json;
  if (!readFile(kSettingsPath, json)) return false;
  std::string err;
  if (settings.loadJson(json, &err)) return true;
  warning = std::string("settings.json was corrupt (") + err + "); using defaults";
  quarantine(kSettingsPath);
  return false;
}

bool Storage::saveSettings(const Settings& settings) {
  return writeFile(kSettingsPath, settings.toJson(false));
}
```

- [ ] **Step 4: Write `firmware/src/Led.h`**

```cpp
#pragma once

#include <cstdint>

// Non-blocking status LED: a base state (on/off) plus short blink bursts on top.
class Led {
 public:
  explicit Led(uint8_t pin) : pin_(pin) {}
  void begin();
  void setBase(bool on);
  void blink(uint8_t count, uint16_t onMs = 80, uint16_t offMs = 120);
  void loop(uint32_t now);

 private:
  void write(bool on);
  uint8_t pin_;
  bool base_ = false;
  uint8_t remaining_ = 0;  // blink phases left (on+off = 2 per blink)
  bool phaseOn_ = false;
  uint16_t onMs_ = 80, offMs_ = 120;
  uint32_t phaseStart_ = 0;
};
```

- [ ] **Step 5: Write `firmware/src/Led.cpp`**

```cpp
#include "Led.h"

#include <Arduino.h>

void Led::begin() {
  pinMode(pin_, OUTPUT);
  write(base_);
}

void Led::write(bool on) {
  digitalWrite(pin_, on ? HIGH : LOW);
}

void Led::setBase(bool on) {
  base_ = on;
  if (remaining_ == 0) write(base_);
}

void Led::blink(uint8_t count, uint16_t onMs, uint16_t offMs) {
  remaining_ = static_cast<uint8_t>(count * 2);
  onMs_ = onMs;
  offMs_ = offMs;
  phaseOn_ = true;
  phaseStart_ = millis();
  write(!base_);  // first phase is "inverted base" so a blink is visible in both base states
}

void Led::loop(uint32_t now) {
  if (remaining_ == 0) return;
  const uint16_t phaseLen = phaseOn_ ? onMs_ : offMs_;
  if (now - phaseStart_ < phaseLen) return;
  --remaining_;
  phaseStart_ = now;
  if (remaining_ == 0) {
    write(base_);
    return;
  }
  phaseOn_ = !phaseOn_;
  write(phaseOn_ ? !base_ : base_);
}
```

- [ ] **Step 6: Write `firmware/src/HaClient.h`**

```cpp
#pragma once

#include <string>

#include "AppState.h"

// Blocking HTTP POST to the Home Assistant webhook. Call only from loop().
namespace HaClient {
// POSTs `payload` as application/json to `url`. 5 s timeout, one retry after 500 ms.
HaResult send(const std::string& url, const std::string& payload);
}
```

- [ ] **Step 7: Write `firmware/src/HaClient.cpp`**

```cpp
#include "HaClient.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

namespace {

int postOnce(const std::string& url, const std::string& payload, std::string& message) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(client, url.c_str())) {
    message = "invalid url";
    return -1;
  }
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(payload.data())), payload.size());
  if (code < 0) {
    message = HTTPClient::errorToString(code).c_str();
  } else {
    message = "HTTP " + std::to_string(code);
  }
  http.end();
  return code;
}

}  // namespace

namespace HaClient {

HaResult send(const std::string& url, const std::string& payload) {
  HaResult r;
  r.any = true;
  if (WiFi.status() != WL_CONNECTED) {
    r.ok = false;
    r.httpCode = 0;
    r.message = "wifi not connected";
    r.atMillis = millis();
    Serial.printf("[ha] skipped: %s\n", r.message.c_str());
    return r;
  }
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (attempt > 0) delay(500);
    std::string message;
    const int code = postOnce(url, payload, message);
    r.httpCode = code;
    r.message = message;
    r.ok = code >= 200 && code < 300;
    r.atMillis = millis();
    Serial.printf("[ha] POST %s attempt %d -> %s\n", url.c_str(), attempt + 1, message.c_str());
    if (r.ok) break;
  }
  return r;
}

}  // namespace HaClient
```

- [ ] **Step 8: Build for the board**

Run (from `firmware/`): `~/.platformio/penv/bin/pio run -e esp32dev`
Expected: `[SUCCESS]`. (These files compile even though `main.cpp` does not use them yet.)

- [ ] **Step 9: Run the native tests to confirm nothing regressed**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native`
Expected: all suites pass.

- [ ] **Step 10: Commit**

```bash
git add firmware/src/AppState.h firmware/src/Storage.h firmware/src/Storage.cpp firmware/src/Led.h firmware/src/Led.cpp firmware/src/HaClient.h firmware/src/HaClient.cpp
git commit -m "Add AppState, LittleFS storage, LED and HA webhook client"
```

---

### Task 7: TagReader (RC522 driver wrapper)

**Files:**
- Create: `firmware/src/TagReader.h`
- Create: `firmware/src/TagReader.cpp`

**Interfaces:**
- Consumes: `PresenceTracker` (Task 5).
- Produces: `class TagReader { using Callback = std::function<void(const std::string&)>; explicit TagReader(uint8_t ssPin); bool begin(); void setGraceMs(uint32_t); void onPlaced(Callback); void onRemoved(Callback); void poll(uint32_t nowMs); bool found() const; uint8_t version() const; bool present() const; static std::string formatUid(const uint8_t* bytes, uint8_t len); }`

- [ ] **Step 1: Write `firmware/src/TagReader.h`**

```cpp
#pragma once

#include <MFRC522DriverPinSimple.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522v2.h>

#include <cstdint>
#include <functional>
#include <string>

#include "PresenceTracker.h"

// Polls the RC522 every 100 ms and reports tag placed / removed events.
// Everything runs on the caller's thread (loop()); callbacks fire from poll().
class TagReader {
 public:
  using Callback = std::function<void(const std::string& uid)>;
  static constexpr uint32_t kPollMs = 100;

  explicit TagReader(uint8_t ssPin);

  // Initialises SPI and the chip. Returns false if the chip does not answer
  // (version register reads 0x00 or 0xFF).
  bool begin();
  void setGraceMs(uint32_t ms) { tracker_.setGraceMs(ms); }
  void onPlaced(Callback cb) { placed_ = std::move(cb); }
  void onRemoved(Callback cb) { removed_ = std::move(cb); }
  void poll(uint32_t nowMs);

  bool found() const { return found_; }
  uint8_t version() const { return version_; }
  bool present() const { return tracker_.present(); }

  static std::string formatUid(const uint8_t* bytes, uint8_t len);

 private:
  bool readUid(std::string& out);

  MFRC522DriverPinSimple ssPin_;
  MFRC522DriverSPI driver_;
  MFRC522 mfrc_;
  PresenceTracker tracker_;
  Callback placed_;
  Callback removed_;
  uint32_t lastPoll_ = 0;
  bool found_ = false;
  uint8_t version_ = 0;
};
```

- [ ] **Step 2: Write `firmware/src/TagReader.cpp`**

```cpp
#include "TagReader.h"

#include <Arduino.h>
#include <SPI.h>

#include <cstdio>

TagReader::TagReader(uint8_t ssPin) : ssPin_(ssPin), driver_(ssPin_), mfrc_(driver_) {}

bool TagReader::begin() {
  SPI.begin();  // VSPI defaults: SCK 18, MISO 19, MOSI 23
  mfrc_.PCD_Init();
  delay(4);  // some boards need a moment after init before the version register is valid
  version_ = static_cast<uint8_t>(mfrc_.PCD_GetVersion());
  found_ = version_ != 0x00 && version_ != 0xFF;
  Serial.printf("[reader] RC522 version 0x%02X %s\n", version_, found_ ? "ok" : "NOT FOUND");
  return found_;
}

std::string TagReader::formatUid(const uint8_t* bytes, uint8_t len) {
  std::string out;
  char buf[4];
  for (uint8_t i = 0; i < len; ++i) {
    std::snprintf(buf, sizeof(buf), "%02X", bytes[i]);
    if (i) out.push_back(':');
    out += buf;
  }
  return out;
}

bool TagReader::readUid(std::string& out) {
  // Reset baud rates and modulation width, as the library's PICC_IsNewCardPresent does.
  driver_.PCD_WriteRegister(MFRC522Constants::PCD_Register::TxModeReg, 0x00);
  driver_.PCD_WriteRegister(MFRC522Constants::PCD_Register::RxModeReg, 0x00);
  driver_.PCD_WriteRegister(MFRC522Constants::PCD_Register::ModWidthReg, 0x26);

  // WUPA wakes both idle and halted cards, so a card that stays on the reader
  // answers every poll even after we halt it.
  uint8_t atqa[2];
  uint8_t atqaSize = sizeof(atqa);
  const MFRC522Constants::StatusCode st = mfrc_.PICC_WakeupA(atqa, &atqaSize);
  if (st != MFRC522Constants::StatusCode::STATUS_OK && st != MFRC522Constants::StatusCode::STATUS_COLLISION) {
    return false;
  }
  if (!mfrc_.PICC_ReadCardSerial()) return false;
  out = formatUid(mfrc_.uid.uidByte, mfrc_.uid.size);
  mfrc_.PICC_HaltA();
  return true;
}

void TagReader::poll(uint32_t nowMs) {
  if (!found_) return;
  if (nowMs - lastPoll_ < kPollMs) return;
  lastPoll_ = nowMs;

  std::string uid;
  const bool seen = readUid(uid);
  const PresenceTracker::Result r = tracker_.update(seen, uid, nowMs);
  if (r.removed) {
    Serial.printf("[reader] removed %s\n", r.removedUid.c_str());
    if (removed_) removed_(r.removedUid);
  }
  if (r.placed) {
    Serial.printf("[reader] placed %s\n", r.placedUid.c_str());
    if (placed_) placed_(r.placedUid);
  }
}
```

- [ ] **Step 3: Build for the board**

Run (from `firmware/`): `~/.platformio/penv/bin/pio run -e esp32dev`
Expected: `[SUCCESS]`. If the compiler reports that `PCD_Register` or `StatusCode` is not a member of `MFRC522Constants`, open `.pio/libdeps/esp32dev/RFID_MFRC522v2/src/MFRC522Constants.h` and use the enum's actual qualified name; the enums are declared inside `class MFRC522Constants`.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/TagReader.h firmware/src/TagReader.cpp
git commit -m "Add TagReader wrapping MFRC522v2 with WUPA presence polling"
```

---

### Task 8: WifiSetup (STA with AP fallback and captive DNS)

**Files:**
- Create: `firmware/src/WifiSetup.h`
- Create: `firmware/src/WifiSetup.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `class WifiSetup { enum class Mode { Sta, Ap }; static constexpr const char* kApSsid = "Jukebox-Setup"; void begin(const std::string& ssid, const std::string& pass, const std::string& hostname); void loop(uint32_t now); Mode mode() const; bool connected() const; String ip() const; int rssi() const; }`

- [ ] **Step 1: Write `firmware/src/WifiSetup.h`**

```cpp
#pragma once

#include <Arduino.h>
#include <DNSServer.h>

#include <cstdint>
#include <string>

// Joins the configured network; if that is impossible (no SSID, or no link for
// 20 s at boot / 60 s later) it opens the "Jukebox-Setup" access point with a
// captive DNS so any site redirects to the setup page.
class WifiSetup {
 public:
  enum class Mode { Sta, Ap };
  static constexpr const char* kApSsid = "Jukebox-Setup";
  static constexpr uint32_t kBootConnectMs = 20000;
  static constexpr uint32_t kLinkLossToApMs = 60000;

  void begin(const std::string& ssid, const std::string& pass, const std::string& hostname);
  void loop(uint32_t now);

  Mode mode() const { return mode_; }
  bool connected() const;
  String ip() const;
  int rssi() const;

 private:
  void startAp();

  DNSServer dns_;
  Mode mode_ = Mode::Ap;
  std::string hostname_;
  uint32_t disconnectedSince_ = 0;
};
```

- [ ] **Step 2: Write `firmware/src/WifiSetup.cpp`**

```cpp
#include "WifiSetup.h"

#include <ESPmDNS.h>
#include <WiFi.h>

void WifiSetup::begin(const std::string& ssid, const std::string& pass, const std::string& hostname) {
  hostname_ = hostname;
  if (ssid.empty()) {
    Serial.println("[wifi] no SSID configured, starting setup AP");
    startAp();
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname_.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("[wifi] connecting to %s", ssid.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kBootConnectMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] connection failed, starting setup AP");
    startAp();
    return;
  }

  mode_ = Mode::Sta;
  disconnectedSince_ = 0;
  Serial.printf("[wifi] connected, ip %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin(hostname_.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[wifi] mDNS %s.local\n", hostname_.c_str());
  }
}

void WifiSetup::startAp() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(kApSsid);
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(53, "*", WiFi.softAPIP());
  mode_ = Mode::Ap;
  Serial.printf("[wifi] AP %s up, ip %s\n", kApSsid, WiFi.softAPIP().toString().c_str());
}

void WifiSetup::loop(uint32_t now) {
  if (mode_ == Mode::Ap) {
    dns_.processNextRequest();
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSince_ = 0;
    return;
  }
  if (disconnectedSince_ == 0) {
    disconnectedSince_ = now;
    Serial.println("[wifi] link lost, waiting for reconnect");
  } else if (now - disconnectedSince_ > kLinkLossToApMs) {
    Serial.println("[wifi] no link for 60 s, falling back to setup AP");
    startAp();
  }
}

bool WifiSetup::connected() const {
  return mode_ == Mode::Sta && WiFi.status() == WL_CONNECTED;
}

String WifiSetup::ip() const {
  return mode_ == Mode::Ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

int WifiSetup::rssi() const {
  return mode_ == Mode::Sta ? WiFi.RSSI() : 0;
}
```

- [ ] **Step 3: Build for the board**

Run (from `firmware/`): `~/.platformio/penv/bin/pio run -e esp32dev`
Expected: `[SUCCESS]`.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/WifiSetup.h firmware/src/WifiSetup.cpp
git commit -m "Add WifiSetup with STA connect and captive setup AP fallback"
```

---

### Task 9: WebApi and main wiring

**Files:**
- Create: `firmware/src/WebApi.h`
- Create: `firmware/src/WebApi.cpp`
- Modify: `firmware/src/main.cpp` (replace the stub entirely)

**Interfaces:**
- Consumes: `AppState`, `Command`, `HaResult` (Task 6), `Storage` (Task 6), `Led` (Task 6), `HaClient::send` (Task 6), `TagReader` (Task 7), `WifiSetup` (Task 8), `HaPayload` (Task 4).
- Produces: `class WebApi { WebApi(AsyncWebServer&, AppState&, Storage&, WifiSetup&); void begin(); }` and a complete `main.cpp`.

Routes (all JSON unless noted):

| Method | Path | Behaviour |
|-------|------|-----------|
| GET | `/api/status` | mode, ip, rssi, uptime_s, free_heap, version, reader_found, reader_version, tag_count, warning, last_scanned {uid, age_ms, known}, last_result {any, ok, http_code, message, age_ms} |
| GET | `/api/tags` | registry JSON |
| PUT | `/api/tags/{uid}` | body = tag object; 200 `{ok:true,uid}`; 400 / 507 |
| DELETE | `/api/tags/{uid}` | 200 `{ok:true}` or 404 |
| POST | `/api/tags/{uid}/play` | enqueue PlayTag; 202 `{queued:true}` or 404 |
| GET | `/api/settings` | masked settings |
| PUT | `/api/settings` | partial update; 200 masked settings; 400 |
| POST | `/api/test` | enqueue TestWebhook; 202 |
| POST | `/api/reboot` | enqueue Reboot; 202 |
| GET | `/` | `index.html` from LittleFS |
| any (AP mode, foreign host) | `*` | 302 to `http://192.168.4.1/` |

- [ ] **Step 1: Write `firmware/src/WebApi.h`**

```cpp
#pragma once

#include <ESPAsyncWebServer.h>

#include <string>

#include "AppState.h"
#include "Storage.h"
#include "WifiSetup.h"

// HTTP JSON API + static UI. Handlers run on the async TCP task: they only
// touch AppState under its mutex and enqueue Commands for loop().
class WebApi {
 public:
  WebApi(AsyncWebServer& server, AppState& state, Storage& storage, WifiSetup& wifi);
  void begin();

 private:
  void registerRoutes();
  void handleStatus(AsyncWebServerRequest* request);
  void handleGetTags(AsyncWebServerRequest* request);
  void handlePutTag(AsyncWebServerRequest* request, JsonVariant& body);
  void handleDeleteTag(AsyncWebServerRequest* request);
  void handlePlayTag(AsyncWebServerRequest* request);
  void handleGetSettings(AsyncWebServerRequest* request);
  void handlePutSettings(AsyncWebServerRequest* request, JsonVariant& body);
  void enqueue(AsyncWebServerRequest* request, Command::Type type, const std::string& uid = "");

  static void sendJson(AsyncWebServerRequest* request, int code, const std::string& body);
  static void sendError(AsyncWebServerRequest* request, int code, const std::string& message);
  // Extracts the UID from "/api/tags/<uid>" or "/api/tags/<uid>/play".
  static std::string uidFromUrl(const String& url);
  static std::string urlDecode(const std::string& in);

  AsyncWebServer& server_;
  AppState& state_;
  Storage& storage_;
  WifiSetup& wifi_;
};
```

- [ ] **Step 2: Write `firmware/src/WebApi.cpp`**

```cpp
#include "WebApi.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>

namespace {

constexpr size_t kMaxBody = 8192;
constexpr const char* kJson = "application/json";

// In AP mode, any request for a host other than our own IP is a captive-portal
// probe (or a user typing a random site): redirect it to the setup page.
class CaptiveHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest* request) const override {
    return request->host() != WiFi.softAPIP().toString();
  }
  void handleRequest(AsyncWebServerRequest* request) override {
    request->redirect("http://" + WiFi.softAPIP().toString() + "/");
  }
};

}  // namespace

WebApi::WebApi(AsyncWebServer& server, AppState& state, Storage& storage, WifiSetup& wifi)
    : server_(server), state_(state), storage_(storage), wifi_(wifi) {}

void WebApi::begin() {
  registerRoutes();
}

void WebApi::sendJson(AsyncWebServerRequest* request, int code, const std::string& body) {
  request->send(code, kJson, body.c_str());
}

void WebApi::sendError(AsyncWebServerRequest* request, int code, const std::string& message) {
  JsonDocument doc;
  doc["error"] = message;
  std::string out;
  serializeJson(doc, out);
  sendJson(request, code, out);
}

std::string WebApi::urlDecode(const std::string& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      const std::string hex = in.substr(i + 1, 2);
      out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
      i += 2;
    } else if (in[i] == '+') {
      out.push_back(' ');
    } else {
      out.push_back(in[i]);
    }
  }
  return out;
}

std::string WebApi::uidFromUrl(const String& url) {
  static const char* kPrefix = "/api/tags/";
  std::string path = url.c_str();
  if (path.rfind(kPrefix, 0) != 0) return "";
  path = path.substr(strlen(kPrefix));
  const size_t slash = path.find('/');
  if (slash != std::string::npos) path = path.substr(0, slash);
  return TagRegistry::normalizeUid(urlDecode(path));
}

void WebApi::registerRoutes() {
  server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* r) { handleStatus(r); });
  server_.on("/api/tags", HTTP_GET, [this](AsyncWebServerRequest* r) { handleGetTags(r); });
  server_.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* r) { handleGetSettings(r); });
  server_.on("/api/test", HTTP_POST, [this](AsyncWebServerRequest* r) { enqueue(r, Command::Type::TestWebhook); });
  server_.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* r) { enqueue(r, Command::Type::Reboot); });

  auto* putTag = new AsyncCallbackJsonWebHandler(
      AsyncURIMatcher::prefix("/api/tags/"),
      [this](AsyncWebServerRequest* r, JsonVariant& body) { handlePutTag(r, body); });
  putTag->setMethod(HTTP_PUT);
  putTag->setMaxContentLength(kMaxBody);
  server_.addHandler(putTag);

  auto* putSettings = new AsyncCallbackJsonWebHandler(
      "/api/settings", [this](AsyncWebServerRequest* r, JsonVariant& body) { handlePutSettings(r, body); });
  putSettings->setMethod(HTTP_PUT);
  putSettings->setMaxContentLength(kMaxBody);
  server_.addHandler(putSettings);

  server_.on(AsyncURIMatcher::prefix("/api/tags/"), HTTP_DELETE, [this](AsyncWebServerRequest* r) { handleDeleteTag(r); });
  server_.on(AsyncURIMatcher::prefix("/api/tags/"), HTTP_POST, [this](AsyncWebServerRequest* r) { handlePlayTag(r); });

  server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");

  server_.addHandler(new CaptiveHandler()).setFilter(ON_AP_FILTER);

  server_.onNotFound([](AsyncWebServerRequest* r) { sendError(r, 404, "not found"); });
}

void WebApi::handleStatus(AsyncWebServerRequest* request) {
  JsonDocument doc;
  const uint32_t now = millis();
  doc["mode"] = wifi_.mode() == WifiSetup::Mode::Ap ? "ap" : "sta";
  doc["ip"] = wifi_.ip();
  doc["rssi"] = wifi_.rssi();
  doc["uptime_s"] = now / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["version"] = JUKEBOX_VERSION;
  {
    std::lock_guard<std::mutex> lock(state_.mutex);
    doc["reader_found"] = state_.readerFound;
    doc["reader_version"] = state_.readerVersion;
    doc["tag_count"] = state_.registry.size();
    doc["warning"] = state_.warning;
    JsonObject scanned = doc["last_scanned"].to<JsonObject>();
    scanned["uid"] = state_.lastScannedUid;
    scanned["age_ms"] = state_.lastScannedUid.empty() ? 0 : now - state_.lastScannedAt;
    scanned["known"] = state_.lastScannedKnown;
    JsonObject result = doc["last_result"].to<JsonObject>();
    result["any"] = state_.lastResult.any;
    result["ok"] = state_.lastResult.ok;
    result["http_code"] = state_.lastResult.httpCode;
    result["message"] = state_.lastResult.message;
    result["age_ms"] = state_.lastResult.any ? now - state_.lastResult.atMillis : 0;
  }
  std::string out;
  serializeJson(doc, out);
  sendJson(request, 200, out);
}

void WebApi::handleGetTags(AsyncWebServerRequest* request) {
  std::string out;
  {
    std::lock_guard<std::mutex> lock(state_.mutex);
    out = state_.registry.toJson();
  }
  sendJson(request, 200, out);
}

void WebApi::handlePutTag(AsyncWebServerRequest* request, JsonVariant& body) {
  const std::string uid = uidFromUrl(request->url());
  if (uid.empty()) return sendError(request, 400, "uid is required");
  Tag tag;
  std::string err;
  if (!TagRegistry::tagFromJson(body.as<JsonVariantConst>(), tag, &err)) return sendError(request, 400, err);

  TagRegistry::UpsertResult result;
  bool saved = false;
  {
    std::lock_guard<std::mutex> lock(state_.mutex);
    result = state_.registry.upsert(uid, tag, &err);
    if (result == TagRegistry::UpsertResult::Ok) saved = storage_.saveRegistry(state_.registry);
  }
  if (result == TagRegistry::UpsertResult::Invalid) return sendError(request, 400, err);
  if (result == TagRegistry::UpsertResult::Full) return sendError(request, 507, err);
  if (!saved) return sendError(request, 500, "could not write tags.json");

  JsonDocument doc;
  doc["ok"] = true;
  doc["uid"] = uid;
  std::string out;
  serializeJson(doc, out);
  sendJson(request, 200, out);
}

void WebApi::handleDeleteTag(AsyncWebServerRequest* request) {
  const std::string uid = uidFromUrl(request->url());
  bool removed = false;
  bool saved = true;
  {
    std::lock_guard<std::mutex> lock(state_.mutex);
    removed = state_.registry.remove(uid);
    if (removed) saved = storage_.saveRegistry(state_.registry);
  }
  if (!removed) return sendError(request, 404, "unknown uid");
  if (!saved) return sendError(request, 500, "could not write tags.json");
  sendJson(request, 200, "{\"ok\":true}");
}

void WebApi::handlePlayTag(AsyncWebServerRequest* request) {
  const String url = request->url();
  if (!url.endsWith("/play")) return sendError(request, 404, "not found");
  const std::string uid = uidFromUrl(url);
  bool known = false;
  {
    std::lock_guard<std::mutex> lock(state_.mutex);
    Tag unused;
    known = state_.registry.find(uid, unused);
  }
  if (!known) return sendError(request, 404, "unknown uid");
  enqueue(request, Command::Type::PlayTag, uid);
}

void WebApi::handleGetSettings(AsyncWebServerRequest* request) {
  std::string out;
  {
    std::lock_guard<std::mutex> lock(state_.mutex);
    out = state_.settings.toJson(true);
  }
  sendJson(request, 200, out);
}

void WebApi::handlePutSettings(AsyncWebServerRequest* request, JsonVariant& body) {
  std::string json;
  serializeJson(body, json);
  std::string err;
  std::string out;
  bool saved = false;
  {
    std::lock_guard<std::mutex> lock(state_.mutex);
    if (!state_.settings.applyJson(json, &err)) {
      // fall through with err set
    } else {
      saved = storage_.saveSettings(state_.settings);
      out = state_.settings.toJson(true);
      state_.commands.push_back({Command::Type::SettingsChanged, ""});
    }
  }
  if (!err.empty()) return sendError(request, 400, err);
  if (!saved) return sendError(request, 500, "could not write settings.json");
  sendJson(request, 200, out);
}

void WebApi::enqueue(AsyncWebServerRequest* request, Command::Type type, const std::string& uid) {
  {
    std::lock_guard<std::mutex> lock(state_.mutex);
    state_.commands.push_back({type, uid});
  }
  sendJson(request, 202, "{\"queued\":true}");
}
```

- [ ] **Step 3: Replace `firmware/src/main.cpp`**

```cpp
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "AppState.h"
#include "HaClient.h"
#include "HaPayload.h"
#include "Led.h"
#include "Storage.h"
#include "TagReader.h"
#include "WebApi.h"
#include "WifiSetup.h"

namespace {

constexpr uint8_t kPinSs = 5;
constexpr uint8_t kPinRst = 21;
constexpr uint8_t kPinLed = 2;

AppState state;
Storage storage;
TagReader reader(kPinSs);
WifiSetup wifi;
AsyncWebServer server(80);
WebApi api(server, state, storage, wifi);
Led led(kPinLed);

void recordResult(const HaResult& result) {
  std::lock_guard<std::mutex> lock(state.mutex);
  state.lastResult = result;
}

// Sends "play" for a known tag. Blocking; call from loop() only.
void sendPlay(const std::string& uid) {
  std::string url, payload;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    Tag tag;
    if (!state.registry.find(uid, tag)) return;
    url = state.settings.webhookUrl();
    payload = HaPayload::play(uid, tag, state.settings);
  }
  const HaResult r = HaClient::send(url, payload);
  recordResult(r);
  if (!r.ok) led.blink(2, 40, 60);
}

void sendControl(const std::string& uid) {
  std::string url, payload;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.settings.onRemove == Settings::OnRemove::None) return;
    Tag tag;
    if (!state.registry.find(uid, tag)) return;
    const char* event = state.settings.onRemove == Settings::OnRemove::Pause ? "pause" : "stop";
    url = state.settings.webhookUrl();
    payload = HaPayload::control(event, uid, tag, state.settings);
  }
  const HaResult r = HaClient::send(url, payload);
  recordResult(r);
  if (!r.ok) led.blink(2, 40, 60);
}

void sendTest() {
  std::string url;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    url = state.settings.webhookUrl();
  }
  recordResult(HaClient::send(url, HaPayload::test()));
}

void onTagPlaced(const std::string& uid) {
  bool known = false;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    Tag unused;
    known = state.registry.find(uid, unused);
    state.lastScannedUid = uid;
    state.lastScannedAt = millis();
    state.lastScannedKnown = known;
  }
  led.blink(known ? 1 : 3);
  if (known) sendPlay(uid);
}

void onTagRemoved(const std::string& uid) {
  sendControl(uid);
}

void processOneCommand() {
  Command cmd;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.commands.empty()) return;
    cmd = state.commands.front();
    state.commands.pop_front();
  }
  switch (cmd.type) {
    case Command::Type::PlayTag:
      sendPlay(cmd.uid);
      break;
    case Command::Type::TestWebhook:
      sendTest();
      break;
    case Command::Type::SettingsChanged: {
      uint32_t grace;
      {
        std::lock_guard<std::mutex> lock(state.mutex);
        grace = state.settings.pauseGraceMs;
      }
      reader.setGraceMs(grace);
      break;
    }
    case Command::Type::Reboot:
      Serial.println("[main] reboot requested");
      delay(200);
      ESP.restart();
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[main] jukebox %s\n", JUKEBOX_VERSION);

  pinMode(kPinRst, OUTPUT);
  digitalWrite(kPinRst, HIGH);  // keep the RC522 out of power-down; the library uses software reset
  led.begin();

  storage.begin();
  std::string warning;
  storage.loadSettings(state.settings, warning);
  if (!warning.empty()) state.warning = warning;
  warning.clear();
  storage.loadRegistry(state.registry, warning);
  if (!warning.empty()) state.warning += (state.warning.empty() ? "" : " ") + warning;
  Serial.printf("[main] %u tags loaded\n", static_cast<unsigned>(state.registry.size()));

  state.readerFound = reader.begin();
  state.readerVersion = reader.version();
  if (!state.readerFound) state.warning += (state.warning.empty() ? "" : " ") + std::string("RC522 reader not found.");
  reader.setGraceMs(state.settings.pauseGraceMs);
  reader.onPlaced(onTagPlaced);
  reader.onRemoved(onTagRemoved);

  wifi.begin(state.settings.wifiSsid, state.settings.wifiPass, state.settings.hostname);
  led.setBase(wifi.mode() == WifiSetup::Mode::Ap);

  api.begin();
  server.begin();
  Serial.printf("[main] web ui at http://%s/\n", wifi.ip().c_str());
}

void loop() {
  const uint32_t now = millis();
  wifi.loop(now);
  led.setBase(wifi.mode() == WifiSetup::Mode::Ap);
  led.loop(now);
  reader.poll(now);
  processOneCommand();
}
```

- [ ] **Step 4: Build for the board**

Run (from `firmware/`): `~/.platformio/penv/bin/pio run -e esp32dev`
Expected: `[SUCCESS]`, with a line like `RAM: [=  ] ..% ... Flash: [====  ] ..%`. Flash must be under 100% of the `min_spiffs` app partition (1.9 MB).

- [ ] **Step 5: Run the native tests to confirm nothing regressed**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native`
Expected: all suites pass.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/WebApi.h firmware/src/WebApi.cpp firmware/src/main.cpp
git commit -m "Add WebApi JSON routes and wire all modules in main"
```

---

### Task 10: Web UI (`data/index.html`)

**Files:**
- Create: `firmware/data/index.html`
- Delete: `firmware/data/.gitkeep`

**Interfaces:**
- Consumes: the JSON API from Task 9.
- Produces: the single-page UI served at `/`.

- [ ] **Step 1: Write `firmware/data/index.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Jukebox</title>
<style>
  :root { --bg:#f5f5f7; --card:#fff; --text:#1d1d1f; --muted:#6e6e73; --accent:#0a66c2; --ok:#1a7f37; --err:#b42318; --line:#e5e5ea; }
  @media (prefers-color-scheme: dark) { :root { --bg:#111; --card:#1c1c1e; --text:#f2f2f7; --muted:#9a9aa0; --line:#2c2c2e; } }
  * { box-sizing: border-box; }
  body { margin:0; font: 15px/1.45 -apple-system, system-ui, Segoe UI, Roboto, sans-serif; background:var(--bg); color:var(--text); }
  main { max-width: 860px; margin: 0 auto; padding: 16px; }
  h1 { font-size: 22px; margin: 8px 0 4px; }
  h2 { font-size: 17px; margin: 0 0 12px; }
  .status { color: var(--muted); font-size: 13px; margin-bottom: 16px; }
  .status b { color: var(--text); }
  .warn { color: var(--err); }
  .card { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 16px; margin-bottom: 16px; }
  table { width: 100%; border-collapse: collapse; font-size: 14px; }
  th, td { text-align: left; padding: 8px 6px; border-bottom: 1px solid var(--line); vertical-align: top; }
  th { color: var(--muted); font-weight: 600; font-size: 12px; text-transform: uppercase; }
  td.uri { word-break: break-all; color: var(--muted); font-size: 12px; }
  label { display: block; font-size: 13px; color: var(--muted); margin: 10px 0 4px; }
  input[type=text], input[type=password], input[type=number], select, textarea {
    width: 100%; padding: 8px 10px; border: 1px solid var(--line); border-radius: 8px; background: var(--bg); color: var(--text); font: inherit; }
  textarea { min-height: 72px; font-family: ui-monospace, Menlo, monospace; font-size: 13px; }
  .row { display: grid; grid-template-columns: 1fr 1fr; gap: 0 16px; }
  @media (max-width: 600px) { .row { grid-template-columns: 1fr; } }
  button { font: inherit; padding: 8px 14px; border-radius: 8px; border: 1px solid var(--line); background: var(--card); color: var(--text); cursor: pointer; }
  button.primary { background: var(--accent); border-color: var(--accent); color: #fff; }
  button.danger { color: var(--err); }
  button.small { padding: 4px 10px; font-size: 13px; }
  .actions { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 14px; align-items: center; }
  .uid { font-family: ui-monospace, Menlo, monospace; font-size: 18px; padding: 10px 12px; border-radius: 8px; background: var(--bg); border: 1px dashed var(--line); }
  .uid.fresh { border-color: var(--accent); color: var(--accent); }
  .msg { font-size: 13px; margin-left: 4px; }
  .msg.ok { color: var(--ok); } .msg.err { color: var(--err); }
  .inline { display: flex; gap: 10px; align-items: center; }
  .inline input[type=range] { flex: 1; }
  .empty { color: var(--muted); padding: 12px 0; }
</style>
</head>
<body>
<main>
  <h1>Jukebox</h1>
  <div class="status" id="status">Loading…</div>

  <section class="card" id="wifiCard" hidden>
    <h2>WiFi setup</h2>
    <p>The box is in setup mode. Enter your home WiFi and save; the box will reboot and join it.</p>
    <label>Network name (SSID)</label><input type="text" id="s_wifi_ssid">
    <label>Password</label><input type="password" id="s_wifi_pass">
    <div class="actions"><button class="primary" id="btnWifiSave">Save and reboot</button><span class="msg" id="wifiMsg"></span></div>
  </section>

  <section class="card">
    <h2>Tags</h2>
    <table id="tagsTable"><thead><tr><th>Name</th><th>Media</th><th>Player</th><th>Vol</th><th></th></tr></thead><tbody></tbody></table>
    <div class="empty" id="tagsEmpty" hidden>No tags yet. Scan an object below to add one.</div>
  </section>

  <section class="card">
    <h2 id="formTitle">Add tag</h2>
    <label>Last scanned tag (scan an object on the box, or type a UID)</label>
    <div class="inline"><div class="uid" id="scanUid" style="flex:1">—</div><button class="small" id="btnUseScan">Use</button></div>
    <label>UID</label><input type="text" id="t_uid" placeholder="04:A3:7F:12">
    <label>Name</label><input type="text" id="t_name" placeholder="Blue dinosaur">
    <label>Media URI</label><input type="text" id="t_uri" placeholder="media-source://media_source/media/1.mp3">
    <div class="row">
      <div><label>Content type</label><input type="text" id="t_content_type" value="music"></div>
      <div><label>Player</label><select id="t_player"></select></div>
    </div>
    <label>Volume</label>
    <div class="inline">
      <input type="checkbox" id="t_vol_default" checked> <span>Use default</span>
      <input type="range" id="t_volume" min="0" max="100" value="40" disabled> <span id="t_volume_label">40%</span>
    </div>
    <label><input type="checkbox" id="t_shuffle"> Shuffle</label>
    <div class="actions">
      <button class="primary" id="btnTagSave">Save tag</button>
      <button id="btnTagCancel" hidden>Cancel</button>
      <span class="msg" id="tagMsg"></span>
    </div>
  </section>

  <section class="card">
    <h2>Settings</h2>
    <div class="row">
      <div><label>Home Assistant URL</label><input type="text" id="s_ha_url" placeholder="http://homeassistant.local:8123"></div>
      <div><label>Webhook ID</label><input type="text" id="s_webhook_id"></div>
    </div>
    <label>Players (one media_player entity ID per line)</label>
    <textarea id="s_players" placeholder="media_player.kitchen&#10;media_player.living_room"></textarea>
    <div class="row">
      <div><label>Default player</label><select id="s_default_player"></select></div>
      <div><label>Default volume</label><div class="inline"><input type="range" id="s_default_volume" min="0" max="100" value="30"><span id="s_default_volume_label">30%</span></div></div>
    </div>
    <div class="row">
      <div><label>When the object is lifted</label>
        <select id="s_on_remove"><option value="none">Keep playing</option><option value="pause">Pause</option><option value="stop">Stop</option></select></div>
      <div><label>Grace period before pause/stop (ms)</label><input type="number" id="s_pause_grace_ms" min="200" max="30000" step="100"></div>
    </div>
    <div class="row">
      <div><label>Hostname (for &lt;hostname&gt;.local)</label><input type="text" id="s_hostname"></div>
      <div><label>WiFi network</label><input type="text" id="s2_wifi_ssid"></div>
    </div>
    <label>WiFi password (leave masked to keep the current one)</label><input type="password" id="s2_wifi_pass">
    <div class="actions">
      <button class="primary" id="btnSettingsSave">Save settings</button>
      <button id="btnTest">Test connection</button>
      <button class="danger" id="btnReboot">Reboot</button>
      <span class="msg" id="settingsMsg"></span>
    </div>
  </section>
</main>

<script>
(() => {
  const $ = (id) => document.getElementById(id);
  const api = async (method, path, body) => {
    const res = await fetch(path, { method, headers: body ? { 'Content-Type': 'application/json' } : {}, body: body ? JSON.stringify(body) : undefined });
    const text = await res.text();
    let data = {};
    try { data = text ? JSON.parse(text) : {}; } catch (e) { data = { error: text }; }
    if (!res.ok) throw new Error(data.error || ('HTTP ' + res.status));
    return data;
  };
  const say = (id, text, ok) => { const el = $(id); el.textContent = text; el.className = 'msg ' + (ok ? 'ok' : 'err'); if (ok) setTimeout(() => { if (el.textContent === text) el.textContent = ''; }, 4000); };
  const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  let settings = { players: [] };
  let tags = {};
  let editingUid = null;
  let lastScanUid = '';

  // ---------- status ----------
  const renderStatus = (s) => {
    const parts = [];
    parts.push('<b>' + (s.mode === 'ap' ? 'Setup mode' : 'Online') + '</b> ' + esc(s.ip));
    if (s.mode === 'sta') parts.push('RSSI ' + s.rssi + ' dBm');
    parts.push('reader ' + (s.reader_found ? 'ok' : '<span class="warn">not found</span>'));
    parts.push(s.tag_count + ' tags');
    if (s.last_result && s.last_result.any) {
      const ago = Math.round(s.last_result.age_ms / 1000);
      parts.push('last HA call ' + (s.last_result.ok ? '<span style="color:var(--ok)">ok</span>' : '<span class="warn">failed: ' + esc(s.last_result.message) + '</span>') + ' (' + ago + 's ago)');
    }
    parts.push('v' + esc(s.version));
    let html = parts.join(' · ');
    if (s.warning) html += '<br><span class="warn">' + esc(s.warning) + '</span>';
    $('status').innerHTML = html;
    $('wifiCard').hidden = s.mode !== 'ap';

    const ls = s.last_scanned || {};
    const el = $('scanUid');
    if (ls.uid) {
      el.textContent = ls.uid + (ls.known ? ' (known)' : ' (new)');
      if (ls.uid !== lastScanUid) { el.classList.add('fresh'); setTimeout(() => el.classList.remove('fresh'), 1500); }
      lastScanUid = ls.uid;
    }
  };
  const pollStatus = async () => { try { renderStatus(await api('GET', '/api/status')); } catch (e) { $('status').textContent = 'Box unreachable: ' + e.message; } };

  // ---------- tags ----------
  const playerOptions = (sel, includeDefault) => {
    sel.innerHTML = '';
    if (includeDefault) sel.add(new Option('(default: ' + (settings.default_player || 'none') + ')', ''));
    else sel.add(new Option('(none)', ''));
    (settings.players || []).forEach((p) => sel.add(new Option(p, p)));
  };
  const renderTags = () => {
    const tbody = $('tagsTable').querySelector('tbody');
    tbody.innerHTML = '';
    const uids = Object.keys(tags).sort((a, b) => tags[a].name.localeCompare(tags[b].name));
    $('tagsEmpty').hidden = uids.length > 0;
    uids.forEach((uid) => {
      const t = tags[uid];
      const vol = t.volume === undefined || t.volume === null ? 'default' : (t.volume < 0 ? 'keep' : Math.round(t.volume * 100) + '%');
      const tr = document.createElement('tr');
      tr.innerHTML = '<td><b>' + esc(t.name) + '</b><br><span class="uri">' + esc(uid) + '</span></td>' +
        '<td class="uri">' + esc(t.uri) + (t.content_type !== 'music' ? ' <i>(' + esc(t.content_type) + ')</i>' : '') + (t.shuffle ? ' 🔀' : '') + '</td>' +
        '<td>' + esc(t.player || '(default)') + '</td><td>' + vol + '</td>' +
        '<td style="white-space:nowrap"><button class="small" data-act="play">▶</button> <button class="small" data-act="edit">Edit</button> <button class="small danger" data-act="del">✕</button></td>';
      tr.querySelector('[data-act=play]').onclick = async () => { try { await api('POST', '/api/tags/' + uid + '/play'); say('tagMsg', 'Play queued for ' + t.name, true); setTimeout(pollStatus, 1500); } catch (e) { say('tagMsg', e.message, false); } };
      tr.querySelector('[data-act=edit]').onclick = () => startEdit(uid);
      tr.querySelector('[data-act=del]').onclick = async () => { if (!confirm('Delete "' + t.name + '"?')) return; try { await api('DELETE', '/api/tags/' + uid); await loadTags(); } catch (e) { say('tagMsg', e.message, false); } };
      tbody.appendChild(tr);
    });
  };
  const loadTags = async () => { tags = (await api('GET', '/api/tags')).tags || {}; renderTags(); };

  const resetForm = () => {
    editingUid = null;
    $('formTitle').textContent = 'Add tag';
    $('btnTagCancel').hidden = true;
    $('t_uid').disabled = false;
    ['t_uid', 't_name', 't_uri'].forEach((id) => $(id).value = '');
    $('t_content_type').value = 'music';
    $('t_player').value = '';
    $('t_vol_default').checked = true; $('t_volume').disabled = true; $('t_volume').value = 40; $('t_volume_label').textContent = '40%';
    $('t_shuffle').checked = false;
  };
  const startEdit = (uid) => {
    const t = tags[uid];
    editingUid = uid;
    $('formTitle').textContent = 'Edit tag';
    $('btnTagCancel').hidden = false;
    $('t_uid').value = uid; $('t_uid').disabled = true;
    $('t_name').value = t.name; $('t_uri').value = t.uri; $('t_content_type').value = t.content_type || 'music';
    $('t_player').value = t.player || '';
    const useDefault = t.volume === undefined || t.volume === null;
    $('t_vol_default').checked = useDefault; $('t_volume').disabled = useDefault;
    if (!useDefault && t.volume >= 0) { $('t_volume').value = Math.round(t.volume * 100); $('t_volume_label').textContent = $('t_volume').value + '%'; }
    $('t_shuffle').checked = !!t.shuffle;
    window.scrollTo({ top: $('formTitle').offsetTop - 12, behavior: 'smooth' });
  };
  $('btnTagCancel').onclick = resetForm;
  $('btnUseScan').onclick = () => { if (lastScanUid) { if (editingUid) resetForm(); $('t_uid').value = lastScanUid; $('t_name').focus(); } };
  $('t_vol_default').onchange = () => { $('t_volume').disabled = $('t_vol_default').checked; };
  $('t_volume').oninput = () => { $('t_volume_label').textContent = $('t_volume').value + '%'; };
  $('btnTagSave').onclick = async () => {
    const uid = (editingUid || $('t_uid').value).trim();
    if (!uid) return say('tagMsg', 'Scan a tag or enter a UID', false);
    const body = { name: $('t_name').value.trim(), uri: $('t_uri').value.trim(), content_type: $('t_content_type').value.trim() || 'music', player: $('t_player').value, shuffle: $('t_shuffle').checked };
    if (!$('t_vol_default').checked) body.volume = Number($('t_volume').value) / 100;
    try { await api('PUT', '/api/tags/' + uid, body); say('tagMsg', 'Saved', true); resetForm(); await loadTags(); } catch (e) { say('tagMsg', e.message, false); }
  };

  // ---------- settings ----------
  const renderSettings = () => {
    $('s_ha_url').value = settings.ha_url || ''; $('s_webhook_id').value = settings.webhook_id || '';
    $('s_players').value = (settings.players || []).join('\n');
    playerOptions($('s_default_player'), false); $('s_default_player').value = settings.default_player || '';
    $('s_default_volume').value = Math.round((settings.default_volume ?? 0.3) * 100); $('s_default_volume_label').textContent = $('s_default_volume').value + '%';
    $('s_on_remove').value = settings.on_remove || 'none';
    $('s_pause_grace_ms').value = settings.pause_grace_ms ?? 1500;
    $('s_hostname').value = settings.hostname || '';
    $('s2_wifi_ssid').value = settings.wifi_ssid || ''; $('s2_wifi_pass').value = settings.wifi_pass || '';
    $('s_wifi_ssid').value = settings.wifi_ssid || ''; $('s_wifi_pass').value = '';
    playerOptions($('t_player'), true);
  };
  const loadSettings = async () => { settings = await api('GET', '/api/settings'); renderSettings(); };
  $('s_default_volume').oninput = () => { $('s_default_volume_label').textContent = $('s_default_volume').value + '%'; };
  $('s_players').oninput = () => { settings.players = $('s_players').value.split('\n').map((s) => s.trim()).filter(Boolean); const cur = $('s_default_player').value; playerOptions($('s_default_player'), false); $('s_default_player').value = cur; };
  $('btnSettingsSave').onclick = async () => {
    const body = {
      ha_url: $('s_ha_url').value, webhook_id: $('s_webhook_id').value,
      players: $('s_players').value.split('\n').map((s) => s.trim()).filter(Boolean),
      default_player: $('s_default_player').value, default_volume: Number($('s_default_volume').value) / 100,
      on_remove: $('s_on_remove').value, pause_grace_ms: Number($('s_pause_grace_ms').value),
      hostname: $('s_hostname').value, wifi_ssid: $('s2_wifi_ssid').value, wifi_pass: $('s2_wifi_pass').value,
    };
    try { settings = await api('PUT', '/api/settings', body); renderSettings(); renderTags(); say('settingsMsg', 'Saved', true); } catch (e) { say('settingsMsg', e.message, false); }
  };
  $('btnTest').onclick = async () => {
    try { await api('POST', '/api/test'); say('settingsMsg', 'Test sent, check the status line', true); setTimeout(pollStatus, 1500); setTimeout(pollStatus, 6000); } catch (e) { say('settingsMsg', e.message, false); }
  };
  $('btnReboot').onclick = async () => { if (!confirm('Reboot the box?')) return; try { await api('POST', '/api/reboot'); say('settingsMsg', 'Rebooting…', true); } catch (e) { say('settingsMsg', e.message, false); } };
  $('btnWifiSave').onclick = async () => {
    try {
      await api('PUT', '/api/settings', { wifi_ssid: $('s_wifi_ssid').value, wifi_pass: $('s_wifi_pass').value });
      await api('POST', '/api/reboot');
      say('wifiMsg', 'Saved. The box is rebooting; reconnect to your home WiFi and open http://' + (settings.hostname || 'jukebox') + '.local', true);
    } catch (e) { say('wifiMsg', e.message, false); }
  };

  // ---------- boot ----------
  (async () => {
    try { await loadSettings(); await loadTags(); } catch (e) { say('tagMsg', e.message, false); }
    resetForm();
    pollStatus();
    setInterval(pollStatus, 1000);
  })();
})();
</script>
</body>
</html>
```

- [ ] **Step 2: Remove the placeholder and build the filesystem image**

```bash
git rm -q firmware/data/.gitkeep
```

Run (from `firmware/`): `~/.platformio/penv/bin/pio run -e esp32dev -t buildfs`
Expected: `[SUCCESS]` and a `.pio/build/esp32dev/littlefs.bin` file.

- [ ] **Step 3: Sanity-check the page in a browser without hardware**

Serve the file locally and open it; the status line must say "Box unreachable" (no API) and the cards must render without JavaScript errors in the console:

```bash
cd firmware/data && python3 -m http.server 8080
```

Open `http://localhost:8080/`. Expected: the page renders, the console shows only failed `/api/...` fetches.

- [ ] **Step 4: Commit**

```bash
git add firmware/data/index.html
git commit -m "Add single-page web UI for tags and settings"
```

---

### Task 11: Home Assistant automation and documentation

**Files:**
- Create: `homeassistant/jukebox_automation.yaml`
- Create: `homeassistant/README.md`
- Create: `firmware/README.md`

**Interfaces:**
- Consumes: the webhook payload contract (Task 4).
- Produces: the automation that plays media, and the user-facing docs.

- [ ] **Step 1: Write `homeassistant/jukebox_automation.yaml`**

```yaml
# Jukebox webhook automation.
# Import: Settings > Automations > (⋮) > Edit in YAML, paste this, save.
# The webhook_id must match the "Webhook ID" in the jukebox settings page.
alias: Jukebox
description: Plays media on a Cast speaker when the RFID jukebox scans a tag
mode: queued
triggers:
  - trigger: webhook
    webhook_id: jukebox
    allowed_methods:
      - POST
    local_only: true
conditions: []
actions:
  - choose:
      - conditions: "{{ trigger.json.event == 'play' }}"
        sequence:
          - if: "{{ trigger.json.volume is defined }}"
            then:
              - action: media_player.volume_set
                target:
                  entity_id: "{{ trigger.json.player }}"
                data:
                  volume_level: "{{ trigger.json.volume }}"
          - action: media_player.play_media
            target:
              entity_id: "{{ trigger.json.player }}"
            data:
              media_content_id: "{{ trigger.json.uri }}"
              media_content_type: "{{ trigger.json.content_type }}"
          - if: "{{ trigger.json.shuffle }}"
            then:
              - action: media_player.shuffle_set
                target:
                  entity_id: "{{ trigger.json.player }}"
                data:
                  shuffle: true
                continue_on_error: true
      - conditions: "{{ trigger.json.event == 'pause' }}"
        sequence:
          - action: media_player.media_pause
            target:
              entity_id: "{{ trigger.json.player }}"
      - conditions: "{{ trigger.json.event == 'stop' }}"
        sequence:
          - action: media_player.media_stop
            target:
              entity_id: "{{ trigger.json.player }}"
      - conditions: "{{ trigger.json.event == 'test' }}"
        sequence:
          - action: persistent_notification.create
            data:
              title: Jukebox
              message: Jukebox connected and the webhook works.
```

- [ ] **Step 2: Write `homeassistant/README.md`**

````markdown
# Home Assistant side

The jukebox sends one JSON POST per event to
`http://<home-assistant>:8123/api/webhook/<webhook_id>`.

| event | fields |
|-------|--------|
| `play` | `uid`, `name`, `uri`, `content_type`, `player`, `volume` (optional, 0–1), `shuffle` |
| `pause` / `stop` | `uid`, `player` |
| `test` | none |

`player` is a `media_player` entity ID. `uri` and `content_type` are passed
straight to `media_player.play_media`, so anything your Cast player accepts
works, for example `media-source://media_source/media/1.mp3` with content type
`music`.

## Install

1. Settings → Automations & Scenes → Create automation → Create new automation.
2. Open the ⋮ menu → Edit in YAML, paste `jukebox_automation.yaml`, save.
3. In the jukebox web UI, set the Home Assistant URL and the webhook ID (`jukebox`
   unless you changed it in the YAML) and press **Test connection**. A
   "Jukebox connected" notification appears in Home Assistant.

The webhook has `local_only: true`, so only devices on your LAN can trigger it.

## Migrating to Music Assistant

When Music Assistant is installed with Spotify as a provider and Google Cast as a
player provider, replace the `media_player.play_media` step with:

```yaml
          - action: music_assistant.play_media
            target:
              entity_id: "{{ trigger.json.player }}"
            data:
              media_id: "{{ trigger.json.uri }}"
              media_type: "{{ trigger.json.content_type }}"
```

and in the jukebox UI point the tags at Spotify URIs (`spotify:playlist:...`,
content type `playlist` / `album` / `track`) and the players at the Music
Assistant `media_player` entities. Nothing changes in the firmware.
````

- [ ] **Step 3: Write `firmware/README.md`**

````markdown
# Jukebox firmware

ESP32 + RFID-RC522. PlatformIO project; Arduino core 3.x (pioarduino platform).

## Build, flash, test

PlatformIO CLI: `~/.platformio/penv/bin/pio` (or `pio` if on your PATH). Run from this folder.

| Command | What |
|---------|------|
| `pio test -e native` | host unit tests for the pure-logic modules |
| `pio run -e esp32dev` | compile firmware |
| `pio run -e esp32dev -t upload` | flash firmware |
| `pio run -e esp32dev -t buildfs` | build the LittleFS image from `data/` |
| `pio run -e esp32dev -t uploadfs` | flash the LittleFS image (the web UI) |
| `pio device monitor` | serial log, 115200 baud |

Flash both the firmware and the filesystem the first time, and again whenever
`data/index.html` changes. Tags and settings live on the same filesystem; a
`uploadfs` erases them, so export `http://jukebox.local/api/tags` first if you
care about them.

## Wiring

| RC522 | ESP32 GPIO |
|-------|-----------|
| SDA/SS | 5 |
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |
| RST | 21 |
| 3.3V / GND | 3.3V / GND |

## First boot and on-device checklist

1. Serial shows `[reader] RC522 version 0x92 ok` (0x91 and 0x88 are fine too; `NOT FOUND` means wiring).
2. With no WiFi configured the box opens the open network `Jukebox-Setup`. Join it;
   the setup page opens (or browse to `http://192.168.4.1/`). The LED is solid on.
3. Enter SSID and password, **Save and reboot**. Serial shows `[wifi] connected, ip ...`.
4. `curl http://jukebox.local/api/status` returns JSON with `"mode":"sta"`.
5. Scan an unknown tag: LED blinks 3 times, the UID appears in the UI under "Last scanned tag".
6. Press **Use**, give it a name and a media URI, **Save tag**. Scan again: LED blinks once,
   the song plays, the status line shows `last HA call ok`.
7. Set "When the object is lifted" to Pause, save, lift the object: after the grace period
   the song pauses.
8. Power-cycle: tags and settings persist.

## Serial log tags

`[main]`, `[storage]`, `[reader]`, `[wifi]`, `[ha]`. A failed webhook call prints the
HTTP code or the transport error and makes the LED double-blink quickly.

## API

See the `WebApi` route table in `docs/superpowers/plans/2026-10-11-rfid-jukebox.md`
or read `src/WebApi.cpp`. Quick examples:

```bash
curl http://jukebox.local/api/status
curl http://jukebox.local/api/tags
curl -X PUT http://jukebox.local/api/tags/04:A3:7F:12 -H 'Content-Type: application/json' \
  -d '{"name":"Dino","uri":"media-source://media_source/media/1.mp3","player":"media_player.kitchen"}'
curl -X POST http://jukebox.local/api/tags/04:A3:7F:12/play
curl -X POST http://jukebox.local/api/test
```
````

- [ ] **Step 4: Validate the automation YAML parses**

```bash
python3 -c "import yaml,sys; yaml.safe_load(open('homeassistant/jukebox_automation.yaml')); print('yaml ok')"
```

Expected: `yaml ok`. If PyYAML is missing, `pip install pyyaml` first.

- [ ] **Step 5: Commit**

```bash
git add homeassistant/ firmware/README.md
git commit -m "Add Home Assistant automation and firmware/HA documentation"
```

---

### Task 12: Final verification

**Files:** none new.

- [ ] **Step 1: Full native test run**

Run (from `firmware/`): `~/.platformio/penv/bin/pio test -e native`
Expected: every suite passes; no warnings about missing sources.

- [ ] **Step 2: Clean board build and filesystem build**

Run (from `firmware/`): `~/.platformio/penv/bin/pio run -e esp32dev -t clean && ~/.platformio/penv/bin/pio run -e esp32dev && ~/.platformio/penv/bin/pio run -e esp32dev -t buildfs`
Expected: both `[SUCCESS]`. Record the Flash usage percentage in the final report.

- [ ] **Step 3: Check the tree is organised as the spec says**

```bash
git ls-files
```

Expected top-level entries: `.gitignore`, `README.md`, `CLAUDE.md`, `docs/`, `firmware/`, `homeassistant/`. No `.pio/` files tracked.

- [ ] **Step 4: Report**

State what was built, test and build results with their actual output lines, and that flashing plus the on-device checklist in `firmware/README.md` remain for the user.
