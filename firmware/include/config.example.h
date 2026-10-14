// Copy this file to include/config.h and fill in your values.
// include/config.h is git-ignored; never commit real credentials.
#pragma once

#define JUKEB_WIFI_SSID     "MyWifi"
#define JUKEB_WIFI_PASSWORD "secret"

// Home Assistant base URL, no trailing slash. Use the IP if .local names do not resolve.
#define JUKEB_HA_URL        "http://homeassistant.local:8123"

// Must equal the Webhook ID entered when adding the JukeB integration.
#define JUKEB_WEBHOOK_ID    "jukeb"

// Free-text name reported in every event; shown in the panel.
#define JUKEB_BOX_ID        "jukeb"
