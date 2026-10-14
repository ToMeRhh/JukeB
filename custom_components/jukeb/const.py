"""Constants for the JukeB integration."""

from homeassistant.const import Platform

DOMAIN = "jukeb"
VERSION = "3.0.0"
MANUFACTURER = "JukeB"
PLATFORMS = [Platform.BINARY_SENSOR, Platform.SWITCH]
SIGNAL_UPDATE = f"{DOMAIN}_update"  # dispatcher: box event, offline timeout or settings change

CONF_WEBHOOK_ID = "webhook_id"
CONF_PLAYER = "player"
DEFAULT_WEBHOOK_ID = "jukeb"
WEBHOOK_ID_PATTERN = r"^[a-z0-9_-]+$"

STORAGE_KEY = "jukeb"
STORAGE_VERSION = 1

EVENT_PRESENT = "present"
EVENT_ABSENT = "absent"
EVENT_HEARTBEAT = "heartbeat"
EVENTS = (EVENT_PRESENT, EVENT_ABSENT, EVENT_HEARTBEAT)

ON_REMOVE_NONE = "none"
ON_REMOVE_PAUSE = "pause"
ON_REMOVE_STOP = "stop"
ON_REMOVE_CHOICES = (ON_REMOVE_NONE, ON_REMOVE_PAUSE, ON_REMOVE_STOP)

MEDIA_TYPE_FILE = "file"

DEFAULT_VOLUME = 0.3
DEFAULT_ON_REMOVE = ON_REMOVE_PAUSE
DEFAULT_GRACE_S = 2
MAX_GRACE_S = 60
MAX_TAGS = 500
MAX_UPLOAD_BYTES = 50 * 1024 * 1024
OFFLINE_AFTER_S = 90

MEDIA_SUBDIR = "jukeb"
MEDIA_CONTENT_TYPE = "music"

PANEL_URL_PATH = "jukeb"
PANEL_TITLE = "JukeB"
PANEL_ICON = "mdi:music-box-multiple"
PANEL_ELEMENT = "jukeb-panel"
STATIC_URL = "/api/jukeb/static"
UPLOAD_URL = "/api/jukeb/upload"

DATA_SETUP_DONE = "jukeb_global_setup_done"
