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
