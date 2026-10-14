#include <Arduino.h>

#include <deque>
#include <string>

#if !__has_include("config.h")
#error "Copy firmware/include/config.example.h to firmware/include/config.h and fill in your WiFi and Home Assistant details"
#endif
#include "config.h"

#include "EventPayload.h"
#include "HaClient.h"
#include "Led.h"
#include "PresenceDebouncer.h"
#include "ReplyParser.h"
#include "TagReader.h"
#include "WifiLink.h"

namespace {

// The RC522 header (SDA SCK MOSI MISO IRQ GND RST 3.3V) lands on the bottom eight pins of the
// DevKit's right-hand row (D5 TX2 RX2 D4 D2 D15 GND 3V3) in order; only GND and RST swap places.
// GPIO16/17 are PSRAM on WROVER modules, so this layout needs a WROOM board.
constexpr uint8_t kSsPin = 5;
constexpr uint8_t kSckPin = 17;
constexpr uint8_t kMosiPin = 16;
constexpr uint8_t kMisoPin = 4;
constexpr uint8_t kRstPin = 15;  // held HIGH; the library uses software reset
constexpr uint8_t kLedPin = 2;
constexpr uint32_t kHeartbeatMs = 30000;
constexpr size_t kQueueMax = 16;

const std::string kWebhookUrl = std::string(JUKEB_HA_URL) + "/api/webhook/" + JUKEB_WEBHOOK_ID;
const EventPayload::BoxInfo kBox{JUKEB_BOX_ID, JUKEB_VERSION};

struct Pending {
  std::string label;  // for the log
  std::string body;
  bool isPresent;     // reply carries "known"
};

std::deque<Pending> queue;
TagReader reader(kSsPin, kSckPin, kMisoPin, kMosiPin);
WifiLink wifi;
Led led(kLedPin);
uint32_t lastHeartbeat = 0;

void enqueue(std::string label, std::string body, bool isPresent) {
  if (queue.size() >= kQueueMax) {
    Serial.printf("[queue] full, dropping oldest (%s)\n", queue.front().label.c_str());
    queue.pop_front();
  }
  queue.push_back(Pending{std::move(label), std::move(body), isPresent});
}

void queueHeartbeat(uint32_t nowMs) {
  lastHeartbeat = nowMs;
  enqueue("heartbeat", EventPayload::heartbeat(kBox, nowMs / 1000, wifi.rssi()), false);
}

void sendOne() {
  const Pending item = queue.front();
  queue.pop_front();
  const HaClient::Response resp = HaClient::post(kWebhookUrl, item.body);
  const ReplyParser::Reply reply = ReplyParser::parse(resp.httpCode, resp.body);
  Serial.printf("[ha] %s -> %s\n", item.label.c_str(), reply.message.c_str());
  if (!reply.ok) {
    led.blink(2, 40, 60);
    return;
  }
  if (item.isPresent) {
    if (reply.known) led.blink(1, 150, 50);
    else led.blink(3, 100, 100);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(kRstPin, OUTPUT);
  digitalWrite(kRstPin, HIGH);
  led.begin();

  Serial.printf("\n[boot] JukeB firmware %s, box id %s\n", JUKEB_VERSION, JUKEB_BOX_ID);
  Serial.printf("[boot] webhook %s\n", kWebhookUrl.c_str());
  Serial.printf("[boot] wifi ssid %s, password %s\n", JUKEB_WIFI_SSID, "********");

  reader.begin();
  wifi.begin(JUKEB_WIFI_SSID, JUKEB_WIFI_PASSWORD, JUKEB_BOX_ID);
  queueHeartbeat(millis());
}

void loop() {
  const uint32_t now = millis();
  wifi.loop(now);
  led.setSlowBlink(!wifi.connected());
  led.loop(now);

  const PresenceEvent ev = reader.poll(now);
  if (ev.type == PresenceEvent::Type::Present) {
    Serial.printf("[reader] present %s\n", ev.uid.c_str());
    enqueue("present " + ev.uid, EventPayload::present(kBox, ev.uid), true);
  } else if (ev.type == PresenceEvent::Type::Absent) {
    Serial.printf("[reader] absent %s\n", ev.uid.c_str());
    enqueue("absent " + ev.uid, EventPayload::absent(kBox, ev.uid), false);
  }

  if (now - lastHeartbeat >= kHeartbeatMs) queueHeartbeat(now);

  if (wifi.connected() && !queue.empty()) sendOne();
}
