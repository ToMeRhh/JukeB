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
