#pragma once

// Optional development defaults.
//
// Normal owner setup happens through the LilyGo setup portal and is stored in
// ESP32 NVS. Copy this file to include/network_config.h only when you want
// compile-time defaults for local testing. The real network_config.h file is
// ignored by git so local Wi-Fi credentials are not committed.

#define WIFI_UPSTREAM_SSID "your-upstream-ssid"
#define WIFI_UPSTREAM_PASSWORD "your-upstream-password"

#define PORTAL_AP_SSID "Home Guest"
#define PORTAL_AP_PASSWORD "change-me-1234"
