// Constants shared by the CikutRail WiThrottle handhelds: Wi-Fi and server
// discovery, the reconnect ladder, the throttle slot and the NVS keys.
// Device-specific pins and UI sizes stay in each project's config.h.

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// WiFi / server
// ---------------------------------------------------------------------------
// SoftAP SSID prefix for the captive-portal provisioning step. The sketch
// appends the last 4 hex digits of the MAC.
#define PROVISION_AP_PREFIX "WiThrottle-"
// Captive-portal password. Empty string => open AP. Keeping it open here
// because the AP is short-lived and only used for first-time setup.
#define PROVISION_AP_PASSWORD ""

// STA connect timeout before falling back to provisioning.
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;

// mDNS service to browse for. JMRI publishes "_withrottle._tcp.local."
#define MDNS_SERVICE_NAME "withrottle"
#define MDNS_SERVICE_PROTO "tcp"
constexpr uint32_t MDNS_QUERY_TIMEOUT_MS = 10000;

// Default WiThrottle TCP port if a manual host:port is configured without a
// port suffix.
constexpr int DEFAULT_WITHROTTLE_PORT = 12090;

// Throttle slot ('0' = first multiThrottle; library README recommends '0'
// for single-throttle clients).
constexpr char THROTTLE_SLOT = '0';

// Backoff schedule (ms) for the server reconnect ladder.
constexpr uint32_t RECONNECT_BACKOFF_MS[] = {1000, 2000, 4000, 8000, 16000};
constexpr size_t RECONNECT_BACKOFF_COUNT =
    sizeof(RECONNECT_BACKOFF_MS) / sizeof(RECONNECT_BACKOFF_MS[0]);

// ---------------------------------------------------------------------------
// NVS keys (kept short; Preferences allows max 15 chars per key)
// ---------------------------------------------------------------------------
#define NVS_NAMESPACE   "wit-throttle"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "pass"
#define NVS_KEY_HOST    "host"   // optional manual host (string)
#define NVS_KEY_PORT    "port"   // optional manual port (uint16)
#define NVS_KEY_LASTLOCO "lastloco" // last acquired loco, e.g. "S10"
