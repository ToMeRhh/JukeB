#include "WebApi.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>

// data/index.html is embedded into the firmware image (board_build.embed_txtfiles),
// so a UI update never touches the LittleFS partition that holds the config.
extern const uint8_t kIndexHtmlStart[] asm("_binary_data_index_html_start");

namespace {

constexpr size_t kMaxBody = 8192;
constexpr const char* kJson = "application/json";
constexpr const char* kHtml = "text/html";

void sendIndex(AsyncWebServerRequest* request) {
  static const size_t len = strlen(reinterpret_cast<const char*>(kIndexHtmlStart));
  AsyncWebServerResponse* res = request->beginResponse(200, kHtml, kIndexHtmlStart, len);
  res->addHeader("Cache-Control", "no-cache");
  request->send(res);
}

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

  server_.on("/", HTTP_GET, sendIndex);
  server_.on("/index.html", HTTP_GET, sendIndex);

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
    if (state_.settings.applyJson(json, &err)) {
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
