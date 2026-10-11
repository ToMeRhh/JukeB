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
