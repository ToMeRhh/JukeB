// Copy this file to include/config.h and fill in your values.
// include/config.h is git-ignored; never commit real credentials.
#pragma once

#define JUKEBOX_WIFI_SSID     "MyWifi"
#define JUKEBOX_WIFI_PASSWORD "secret"

// Home Assistant base URL, no trailing slash. Use the IP if .local names do not resolve.
#define JUKEBOX_HA_URL        "http://homeassistant.local:8123"

// Must equal the Webhook ID entered when adding the Jukebox integration.
#define JUKEBOX_WEBHOOK_ID    "jukebox"

// Free-text name reported in every event; shown in the panel.
#define JUKEBOX_BOX_ID        "jukebox"
