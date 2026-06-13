#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <WiFi.h>

#if __has_include("network_config.h")
#include "network_config.h"
#endif

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#if defined(CONFIG_LWIP_IP_FORWARD) && CONFIG_LWIP_IP_FORWARD && \
    defined(CONFIG_LWIP_IPV4_NAPT) && CONFIG_LWIP_IPV4_NAPT
#define MOBILE_BLOCKER_NAPT_AVAILABLE 1
extern "C" {
#include "lwip/lwip_napt.h"
}
#else
#define MOBILE_BLOCKER_NAPT_AVAILABLE 0
#endif

#ifndef WIFI_UPSTREAM_SSID
#define WIFI_UPSTREAM_SSID ""
#endif

#ifndef WIFI_UPSTREAM_PASSWORD
#define WIFI_UPSTREAM_PASSWORD ""
#endif

#ifndef PORTAL_AP_SSID
#define PORTAL_AP_SSID "Home Guest"
#endif

#ifndef PORTAL_AP_PASSWORD
#define PORTAL_AP_PASSWORD "homeguest123"
#endif

namespace {
constexpr byte kDnsPort = 53;
constexpr uint16_t kHttpPort = 80;
constexpr uint32_t kDisplayRefreshMs = 1000;
constexpr uint32_t kWifiReconnectMs = 10000;
constexpr uint32_t kDebounceMs = 35;
constexpr uint32_t kLongPressMs = 1600;
constexpr uint32_t kDefaultSessionMinutes = 120;
constexpr uint8_t kBoardPowerEnablePin = 15;
constexpr uint8_t kButtonPagePin = 0;
constexpr uint8_t kButtonActionPin = 14;
const IPAddress kPortalIp(192, 168, 42, 1);
const IPAddress kPortalGateway(192, 168, 42, 1);
const IPAddress kPortalSubnet(255, 255, 255, 0);

struct GatewaySettings {
    String upstreamSsid;
    String upstreamPassword;
    String guestApSsid;
    String guestApPassword;
    String guestCode;
    uint32_t sessionMinutes;
};

struct ButtonState {
    bool stablePressed = false;
    bool lastReadPressed = false;
    bool longHandled = false;
    uint32_t lastChangedMs = 0;
    uint32_t pressedAtMs = 0;
};

TFT_eSPI display;
DNSServer dnsServer;
WebServer webServer(kHttpPort);
Preferences preferences;
GatewaySettings settings;
ButtonState pageButton;
ButtonState actionButton;

uint32_t lastDisplayRefreshMs = 0;
uint32_t lastWifiReconnectMs = 0;
uint32_t guestAccessEndsAtMs = 0;
uint8_t screenPage = 0;
bool setupMode = false;
bool naptEnabled = false;
bool guestAccessActive = false;
String portalMessage;

String valueOrDefault(const String& value, const char* fallback) {
    return value.length() > 0 ? value : String(fallback);
}

String htmlEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length());

    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        switch (c) {
            case '&':
                escaped += F("&amp;");
                break;
            case '<':
                escaped += F("&lt;");
                break;
            case '>':
                escaped += F("&gt;");
                break;
            case '"':
                escaped += F("&quot;");
                break;
            default:
                escaped += c;
                break;
        }
    }

    return escaped;
}

String jsonEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length());

    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        switch (c) {
            case '\\':
                escaped += F("\\\\");
                break;
            case '"':
                escaped += F("\\\"");
                break;
            case '\n':
                escaped += F("\\n");
                break;
            case '\r':
                escaped += F("\\r");
                break;
            case '\t':
                escaped += F("\\t");
                break;
            default:
                escaped += c;
                break;
        }
    }

    return escaped;
}

String formatDuration(uint32_t totalSeconds) {
    const uint32_t hours = totalSeconds / 3600;
    const uint32_t minutes = (totalSeconds % 3600) / 60;
    const uint32_t seconds = totalSeconds % 60;

    char buffer[16];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%luh %02lum",
                 static_cast<unsigned long>(hours),
                 static_cast<unsigned long>(minutes));
    } else {
        snprintf(buffer, sizeof(buffer), "%02lu:%02lu",
                 static_cast<unsigned long>(minutes),
                 static_cast<unsigned long>(seconds));
    }
    return String(buffer);
}

uint32_t remainingAccessSeconds() {
    if (!guestAccessActive) {
        return 0;
    }

    const int32_t remainingMs = static_cast<int32_t>(guestAccessEndsAtMs - millis());
    if (remainingMs <= 0) {
        return 0;
    }

    return static_cast<uint32_t>(remainingMs / 1000);
}

bool upstreamConfigured() {
    return settings.upstreamSsid.length() > 0;
}

bool upstreamConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String upstreamIpText() {
    return upstreamConnected() ? WiFi.localIP().toString() : String(F("not connected"));
}

String gatewayModeText() {
    if (setupMode) {
        return F("setup mode");
    }

#if MOBILE_BLOCKER_NAPT_AVAILABLE
    if (!guestAccessActive) {
        return F("closed");
    }
    if (!upstreamConnected()) {
        return F("waiting for upstream");
    }
    return naptEnabled ? F("guest access open") : F("opening gateway");
#else
    return F("NAT unavailable");
#endif
}

void loadSettings() {
    preferences.begin("gateway", false);

    settings.upstreamSsid = valueOrDefault(preferences.getString("up_ssid", WIFI_UPSTREAM_SSID),
                                           WIFI_UPSTREAM_SSID);
    settings.upstreamPassword =
        valueOrDefault(preferences.getString("up_pass", WIFI_UPSTREAM_PASSWORD),
                       WIFI_UPSTREAM_PASSWORD);
    settings.guestApSsid =
        valueOrDefault(preferences.getString("ap_ssid", PORTAL_AP_SSID), PORTAL_AP_SSID);
    settings.guestApPassword = valueOrDefault(preferences.getString("ap_pass", PORTAL_AP_PASSWORD),
                                              PORTAL_AP_PASSWORD);
    settings.guestCode = valueOrDefault(preferences.getString("guest_code", "123456"), "123456");
    settings.sessionMinutes = preferences.getUInt("minutes", kDefaultSessionMinutes);

    if (settings.guestApSsid.length() == 0) {
        settings.guestApSsid = F("Home Guest");
    }
    if (settings.guestApPassword.length() > 0 && settings.guestApPassword.length() < 8) {
        settings.guestApPassword = F("homeguest123");
    }
    if (settings.guestCode.length() < 4) {
        settings.guestCode = F("123456");
    }
    if (settings.sessionMinutes < 5 || settings.sessionMinutes > 1440) {
        settings.sessionMinutes = kDefaultSessionMinutes;
    }
}

void saveSettings() {
    preferences.putString("up_ssid", settings.upstreamSsid);
    preferences.putString("up_pass", settings.upstreamPassword);
    preferences.putString("ap_ssid", settings.guestApSsid);
    preferences.putString("ap_pass", settings.guestApPassword);
    preferences.putString("guest_code", settings.guestCode);
    preferences.putUInt("minutes", settings.sessionMinutes);
}

bool readAndClearSetupNextFlag() {
    const bool setupNext = preferences.getBool("setup_next", false);
    if (setupNext) {
        preferences.putBool("setup_next", false);
    }
    return setupNext;
}

void setGatewayOpen(bool open) {
#if MOBILE_BLOCKER_NAPT_AVAILABLE
    if (open && upstreamConnected() && !naptEnabled) {
        ip_napt_enable(static_cast<uint32_t>(WiFi.softAPIP()), 1);
        naptEnabled = true;
        Serial.println(F("NAPT enabled for guest AP"));
    } else if ((!open || !upstreamConnected()) && naptEnabled) {
        ip_napt_enable(static_cast<uint32_t>(WiFi.softAPIP()), 0);
        naptEnabled = false;
        Serial.println(F("NAPT disabled for guest AP"));
    }
#else
    naptEnabled = false;
#endif
}

void startGuestAccess() {
    guestAccessActive = true;
    guestAccessEndsAtMs = millis() + settings.sessionMinutes * 60UL * 1000UL;
    setGatewayOpen(true);
}

void endGuestAccess() {
    guestAccessActive = false;
    guestAccessEndsAtMs = 0;
    setGatewayOpen(false);
}

void expireGuestAccessIfNeeded() {
    if (guestAccessActive && remainingAccessSeconds() == 0) {
        endGuestAccess();
        portalMessage = F("Guest access expired.");
    }
}

String statusJson() {
    String body;
    body.reserve(460);
    body += F("{\"mode\":\"");
    body += setupMode ? F("setup") : F("guest");
    body += F("\",\"guestApSsid\":\"");
    body += jsonEscape(settings.guestApSsid);
    body += F("\",\"portalIp\":\"");
    body += WiFi.softAPIP().toString();
    body += F("\",\"clients\":");
    body += WiFi.softAPgetStationNum();
    body += F(",\"upstreamConfigured\":");
    body += upstreamConfigured() ? F("true") : F("false");
    body += F(",\"upstreamConnected\":");
    body += upstreamConnected() ? F("true") : F("false");
    body += F(",\"upstreamSsid\":\"");
    body += jsonEscape(settings.upstreamSsid);
    body += F("\",\"upstreamIp\":\"");
    body += jsonEscape(upstreamIpText());
    body += F("\",\"guestAccessActive\":");
    body += guestAccessActive ? F("true") : F("false");
    body += F(",\"remainingSeconds\":");
    body += remainingAccessSeconds();
    body += F(",\"naptEnabled\":");
    body += naptEnabled ? F("true") : F("false");
    body += F(",\"gatewayMode\":\"");
    body += jsonEscape(gatewayModeText());
    body += F("\"}");
    return body;
}

String pageShell(const String& title, const String& bodyContent) {
    String body;
    body.reserve(bodyContent.length() + 1600);
    body += F("<!doctype html><html lang=\"en\"><head>"
              "<meta charset=\"utf-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
              "<title>");
    body += htmlEscape(title);
    body += F("</title><style>"
              ":root{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif;"
              "color:#f5f5f2;background:#151515;color-scheme:dark}"
              "body{margin:0;padding:24px;line-height:1.45}"
              "main{max-width:760px;margin:0 auto}"
              "h1{font-size:1.7rem;margin:0 0 16px}"
              "h2{font-size:1.1rem;margin:0 0 12px}"
              ".panel{border:1px solid #3b3b35;border-radius:8px;padding:18px;margin:14px 0;"
              "background:#20201c}"
              "label{display:block;margin:14px 0 6px;color:#d5d2c7}"
              "input,select{width:100%;box-sizing:border-box;border:1px solid #59594e;"
              "border-radius:6px;background:#111;color:#f5f5f2;padding:11px;font-size:1rem}"
              "button{appearance:none;border:0;border-radius:6px;padding:12px 16px;"
              "background:#2f80ed;color:white;font-weight:700;font-size:1rem}"
              ".secondary{background:#3f3f38}.danger{background:#b23b3b}"
              "dl{display:grid;grid-template-columns:max-content 1fr;gap:8px 16px;margin:0}"
              "dt{color:#aaa69a}dd{margin:0;word-break:break-word}"
              ".ok{color:#56d98d}.warn{color:#ffcf5c}.error{color:#ff8a8a}"
              ".message{border-left:4px solid #56d98d;padding-left:12px;color:#f5f5f2}"
              "p{margin:0 0 12px}.small{font-size:.9rem;color:#aaa69a}"
              "</style></head><body><main>");
    body += bodyContent;
    body += F("</main></body></html>");
    return body;
}

String networkOptionsHtml() {
    String options;
    options.reserve(1500);
    const int networkCount = WiFi.scanNetworks(false, true);
    if (networkCount <= 0) {
        options += F("<option value=\"\">No networks found</option>");
        return options;
    }

    for (int i = 0; i < networkCount; ++i) {
        const String ssid = WiFi.SSID(i);
        options += F("<option value=\"");
        options += htmlEscape(ssid);
        options += F("\"");
        if (ssid == settings.upstreamSsid) {
            options += F(" selected");
        }
        options += F(">");
        options += htmlEscape(ssid);
        options += F(" (");
        options += WiFi.RSSI(i);
        options += F(" dBm");
        options += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? F(", open") : F(", secured");
        options += F(")</option>");
    }

    return options;
}

String setupHtml() {
    String body;
    body.reserve(5200);
    body += F("<h1>Gateway setup</h1>");
    if (portalMessage.length() > 0) {
        body += F("<section class=\"panel message\"><p>");
        body += htmlEscape(portalMessage);
        body += F("</p></section>");
    }
    body += F("<section class=\"panel\"><h2>Upstream home Wi-Fi</h2>"
              "<form method=\"post\" action=\"/admin/save\">"
              "<label for=\"upstream_ssid\">Broadcasting SSID</label>"
              "<select id=\"upstream_ssid\" name=\"upstream_ssid\">");
    body += networkOptionsHtml();
    body += F("</select>"
              "<label for=\"manual_ssid\">Manual SSID override</label>"
              "<input id=\"manual_ssid\" name=\"manual_ssid\" placeholder=\"Optional hidden SSID\">"
              "<label for=\"upstream_password\">Home Wi-Fi password</label>"
              "<input id=\"upstream_password\" name=\"upstream_password\" type=\"password\" "
              "placeholder=\"Leave blank only for open Wi-Fi or to keep current password\">"
              "<p class=\"small\">This credential is stored only on the LilyGo in NVS. It is not "
              "shown on the guest portal.</p>");
    body += F("<h2>Guest network</h2>"
              "<label for=\"guest_ap_ssid\">Guest SSID</label>"
              "<input id=\"guest_ap_ssid\" name=\"guest_ap_ssid\" value=\"");
    body += htmlEscape(settings.guestApSsid);
    body += F("\">"
              "<label for=\"guest_ap_password\">Guest AP password</label>"
              "<input id=\"guest_ap_password\" name=\"guest_ap_password\" value=\"");
    body += htmlEscape(settings.guestApPassword);
    body += F("\" minlength=\"8\" placeholder=\"8+ chars, or blank for open AP\">"
              "<label for=\"guest_code\">Guest portal access code</label>"
              "<input id=\"guest_code\" name=\"guest_code\" value=\"");
    body += htmlEscape(settings.guestCode);
    body += F("\" minlength=\"4\">"
              "<label for=\"session_minutes\">Access duration in minutes</label>"
              "<input id=\"session_minutes\" name=\"session_minutes\" type=\"number\" min=\"5\" "
              "max=\"1440\" value=\"");
    body += settings.sessionMinutes;
    body += F("\"><p><button type=\"submit\">Save settings</button></p></form></section>");
    body += F("<section class=\"panel\"><h2>Device controls</h2>"
              "<p>After saving, reboot normally to start the guest gateway. Hold the right-hand "
              "button while booting to return to setup mode.</p>"
              "<form method=\"post\" action=\"/admin/reboot\"><button class=\"secondary\" "
              "type=\"submit\">Reboot now</button></form></section>");
    return pageShell(F("Gateway setup"), body);
}

String guestHtml() {
    String body;
    body.reserve(3600);
    body += F("<h1>");
    body += htmlEscape(settings.guestApSsid);
    body += F("</h1>");
    if (portalMessage.length() > 0) {
        body += F("<section class=\"panel message\"><p>");
        body += htmlEscape(portalMessage);
        body += F("</p></section>");
    }
    body += F("<section class=\"panel\"><dl>");
    body += F("<dt>Status</dt><dd class=\"");
    body += guestAccessActive ? F("ok\">Access open") : F("warn\">Login required");
    body += F("</dd><dt>Time remaining</dt><dd>");
    body += guestAccessActive ? formatDuration(remainingAccessSeconds()) : F("-");
    body += F("</dd><dt>Upstream</dt><dd class=\"");
    body += upstreamConnected() ? F("ok\">Connected") : F("warn\">Offline");
    body += F("</dd><dt>Gateway</dt><dd>");
    body += htmlEscape(gatewayModeText());
    body += F("</dd></dl></section>");

    if (!guestAccessActive) {
        body += F("<section class=\"panel\"><h2>Guest access</h2>"
                  "<p>Enter the access code from your host to use this guest network.</p>"
                  "<form method=\"post\" action=\"/guest/login\">"
                  "<label for=\"code\">Access code</label>"
                  "<input id=\"code\" name=\"code\" inputmode=\"numeric\" autocomplete=\"one-time-code\">"
                  "<p><button type=\"submit\">Start access</button></p></form></section>");
    } else {
        body += F("<section class=\"panel\"><p>You can now use the guest network. Keep this page "
                  "open if you want to watch the remaining time.</p></section>");
    }

    body += F("<section class=\"panel\"><p class=\"small\">This portal does not ask for, reveal, "
              "or share the underlying home Wi-Fi password.</p></section>");
    return pageShell(settings.guestApSsid, body);
}

void sendPortal() {
    webServer.sendHeader(F("Cache-Control"), F("no-store"));
    webServer.send(200, F("text/html"), setupMode ? setupHtml() : guestHtml());
}

void handleAdminSave() {
    if (!setupMode) {
        webServer.send(403, F("text/plain"), F("Admin setup is locked. Reboot into setup mode."));
        return;
    }

    const String manualSsid = webServer.arg(F("manual_ssid"));
    const String selectedSsid = webServer.arg(F("upstream_ssid"));
    settings.upstreamSsid = manualSsid.length() > 0 ? manualSsid : selectedSsid;

    const String upstreamPassword = webServer.arg(F("upstream_password"));
    if (upstreamPassword.length() > 0 || settings.upstreamPassword.length() == 0) {
        settings.upstreamPassword = upstreamPassword;
    }

    settings.guestApSsid = valueOrDefault(webServer.arg(F("guest_ap_ssid")), "Home Guest");
    settings.guestApPassword = webServer.arg(F("guest_ap_password"));
    settings.guestCode = valueOrDefault(webServer.arg(F("guest_code")), "123456");
    settings.sessionMinutes = webServer.arg(F("session_minutes")).toInt();

    if (settings.guestApPassword.length() > 0 && settings.guestApPassword.length() < 8) {
        settings.guestApPassword = F("homeguest123");
        portalMessage = F("Guest AP password was too short, so it was reset to homeguest123.");
    } else {
        portalMessage = F("Settings saved. Reboot normally to start the guest gateway.");
    }
    if (settings.sessionMinutes < 5 || settings.sessionMinutes > 1440) {
        settings.sessionMinutes = kDefaultSessionMinutes;
    }
    if (settings.guestCode.length() < 4) {
        settings.guestCode = F("123456");
    }

    saveSettings();
    sendPortal();
}

void handleGuestLogin() {
    if (setupMode) {
        sendPortal();
        return;
    }

    const String code = webServer.arg(F("code"));
    if (code == settings.guestCode) {
        startGuestAccess();
        portalMessage = String(F("Access granted for ")) + settings.sessionMinutes + F(" minutes.");
    } else {
        portalMessage = F("Access code not accepted.");
    }

    sendPortal();
}

void handleAdminReboot() {
    if (!setupMode) {
        webServer.send(403, F("text/plain"), F("Admin setup is locked."));
        return;
    }

    webServer.send(200, F("text/html"), pageShell(F("Rebooting"), F("<h1>Rebooting...</h1>")));
    delay(500);
    ESP.restart();
}

void configureWebServer() {
    webServer.on(F("/"), HTTP_GET, sendPortal);
    webServer.on(F("/admin"), HTTP_GET, sendPortal);
    webServer.on(F("/admin/save"), HTTP_POST, handleAdminSave);
    webServer.on(F("/admin/reboot"), HTTP_POST, handleAdminReboot);
    webServer.on(F("/guest/login"), HTTP_POST, handleGuestLogin);
    webServer.on(F("/status"), HTTP_GET, []() {
        webServer.sendHeader(F("Cache-Control"), F("no-store"));
        webServer.send(200, F("application/json"), statusJson());
    });

    webServer.on(F("/generate_204"), HTTP_GET, sendPortal);
    webServer.on(F("/gen_204"), HTTP_GET, sendPortal);
    webServer.on(F("/hotspot-detect.html"), HTTP_GET, sendPortal);
    webServer.on(F("/library/test/success.html"), HTTP_GET, sendPortal);
    webServer.on(F("/connecttest.txt"), HTTP_GET, sendPortal);
    webServer.on(F("/ncsi.txt"), HTTP_GET, sendPortal);
    webServer.on(F("/canonical.html"), HTTP_GET, sendPortal);
    webServer.onNotFound(sendPortal);
    webServer.begin();
}

void configureDisplay() {
    pinMode(kBoardPowerEnablePin, OUTPUT);
    digitalWrite(kBoardPowerEnablePin, HIGH);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

    display.init();
    display.setRotation(1);
    display.fillScreen(TFT_BLACK);
    display.setTextDatum(TL_DATUM);
}

void drawFooter() {
    display.fillRect(0, 151, 320, 19, TFT_DARKGREY);
    display.setTextFont(1);
    display.setTextColor(TFT_WHITE, TFT_DARKGREY);
    display.drawString(F("BOOT: page"), 8, 157);
    display.drawString(setupMode ? F("IO14: reboot") : F("IO14: start/end"),
                       204,
                       157);
}

void drawHeader(const String& title, uint16_t color) {
    display.fillScreen(TFT_BLACK);
    display.fillRect(0, 0, 320, 28, color);
    display.setTextColor(TFT_BLACK, color);
    display.setTextFont(2);
    display.drawString(title, 10, 7);
}

void drawDashboard() {
    const bool open = guestAccessActive && naptEnabled;
    drawHeader(F("Guest gateway"), setupMode ? TFT_ORANGE : (open ? TFT_GREEN : TFT_CYAN));
    display.setTextFont(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString(String(F("Mode: ")) + (setupMode ? F("SETUP") : F("GUEST")), 10, 38);
    display.drawString(String(F("AP: ")) + (setupMode ? F("mobile-blocker-setup")
                                                       : settings.guestApSsid),
                       10,
                       58);
    display.drawString(String(F("Clients: ")) + WiFi.softAPgetStationNum(), 10, 78);
    display.setTextColor(open ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    display.drawString(String(F("Access: ")) +
                           (guestAccessActive ? formatDuration(remainingAccessSeconds())
                                              : F("closed")),
                       10,
                       100);
    display.setTextColor(upstreamConnected() ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    display.drawString(upstreamConnected() ? F("Home Wi-Fi: connected")
                                           : F("Home Wi-Fi: offline"),
                       10,
                       122);
    drawFooter();
}

void drawUpstream() {
    drawHeader(F("Home Wi-Fi"), upstreamConnected() ? TFT_GREEN : TFT_ORANGE);
    display.setTextFont(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString(String(F("SSID: ")) +
                           (settings.upstreamSsid.length() > 0 ? settings.upstreamSsid
                                                               : F("not set")),
                       10,
                       42);
    display.drawString(String(F("IP: ")) + upstreamIpText(), 10, 66);
    const String rssiText = upstreamConnected() ? String(WiFi.RSSI()) + F(" dBm") : String(F("-"));
    display.drawString(String(F("RSSI: ")) + rssiText, 10, 90);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.drawString(F("Hold IO14 at boot for setup"), 10, 120);
    drawFooter();
}

void drawGuest() {
    drawHeader(F("Guest access"), guestAccessActive ? TFT_GREEN : TFT_RED);
    display.setTextFont(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString(String(F("Portal: ")) + WiFi.softAPIP().toString(), 10, 42);
    display.drawString(String(F("Code: ")) + settings.guestCode, 10, 66);
    display.drawString(String(F("Duration: ")) + settings.sessionMinutes + F(" min"), 10, 90);
    display.setTextColor(naptEnabled ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    display.drawString(String(F("Gateway: ")) + gatewayModeText(), 10, 116);
    drawFooter();
}

void drawSetupHelp() {
    drawHeader(F("Setup"), TFT_ORANGE);
    display.setTextFont(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString(F("1. Join Wi-Fi:"), 10, 40);
    display.drawString(F("mobile-blocker-setup"), 24, 61);
    display.drawString(F("2. Open:"), 10, 86);
    display.drawString(WiFi.softAPIP().toString(), 24, 107);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.drawString(F("Save, then reboot normally"), 10, 132);
    drawFooter();
}

void drawStatus() {
    switch (screenPage) {
        case 0:
            drawDashboard();
            break;
        case 1:
            drawUpstream();
            break;
        case 2:
            drawGuest();
            break;
        default:
            drawSetupHelp();
            break;
    }
}

void connectUpstream() {
    if (!upstreamConfigured()) {
        Serial.println(F("No upstream SSID configured; running setup/portal only"));
        return;
    }

    if (upstreamConnected() || WiFi.status() == WL_CONNECT_FAILED ||
        WiFi.status() == WL_IDLE_STATUS) {
        WiFi.disconnect(false, false);
    }

    Serial.printf("Connecting upstream SSID '%s'\n", settings.upstreamSsid.c_str());
    WiFi.begin(settings.upstreamSsid.c_str(), settings.upstreamPassword.c_str());
}

void startNetworks() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(kPortalIp, kPortalGateway, kPortalSubnet);

    const String apSsid = setupMode ? String(F("mobile-blocker-setup")) : settings.guestApSsid;
    const String apPassword = setupMode ? String(F("setup12345")) : settings.guestApPassword;
    const bool apStarted = apPassword.length() >= 8
                               ? WiFi.softAP(apSsid.c_str(), apPassword.c_str())
                               : WiFi.softAP(apSsid.c_str());

    Serial.printf("Portal AP %s: %s (%s)\n",
                  apStarted ? "started" : "failed",
                  apSsid.c_str(),
                  WiFi.softAPIP().toString().c_str());

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(kDnsPort, F("*"), WiFi.softAPIP());

    if (!setupMode) {
        connectUpstream();
    }
}

void maybeReconnectUpstream(uint32_t now) {
    if (setupMode || !upstreamConfigured() || upstreamConnected()) {
        return;
    }

    if (now - lastWifiReconnectMs < kWifiReconnectMs) {
        return;
    }

    lastWifiReconnectMs = now;
    connectUpstream();
}

void maybeRefreshDisplay(uint32_t now) {
    if (now - lastDisplayRefreshMs < kDisplayRefreshMs) {
        return;
    }

    lastDisplayRefreshMs = now;
    drawStatus();
    Serial.printf("mode=%s ap_ip=%s clients=%d upstream=%s guest_active=%s remaining=%lu "
                  "gateway=%s\n",
                  setupMode ? "setup" : "guest",
                  WiFi.softAPIP().toString().c_str(),
                  WiFi.softAPgetStationNum(),
                  upstreamConnected() ? "connected" : "offline",
                  guestAccessActive ? "yes" : "no",
                  static_cast<unsigned long>(remainingAccessSeconds()),
                  gatewayModeText().c_str());
}

void handlePageButtonShortPress() {
    screenPage = (screenPage + 1) % 4;
    drawStatus();
}

void handleActionButtonShortPress() {
    if (setupMode) {
        ESP.restart();
        return;
    }

    if (guestAccessActive) {
        endGuestAccess();
        portalMessage = F("Guest access stopped from the LilyGo.");
    } else {
        startGuestAccess();
        portalMessage = F("Guest access started from the LilyGo.");
    }
    drawStatus();
}

void handleActionButtonLongPress() {
    preferences.putBool("setup_next", true);
    display.fillScreen(TFT_BLACK);
    display.setTextFont(2);
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    display.drawString(F("Restarting into setup..."), 18, 70);
    delay(500);
    ESP.restart();
}

void updateButton(ButtonState& button,
                  uint8_t pin,
                  void (*shortPressHandler)(),
                  void (*longPressHandler)()) {
    const uint32_t now = millis();
    const bool readPressed = digitalRead(pin) == LOW;

    if (readPressed != button.lastReadPressed) {
        button.lastReadPressed = readPressed;
        button.lastChangedMs = now;
    }

    if (now - button.lastChangedMs < kDebounceMs) {
        return;
    }

    if (readPressed != button.stablePressed) {
        button.stablePressed = readPressed;
        if (button.stablePressed) {
            button.pressedAtMs = now;
            button.longHandled = false;
        } else if (!button.longHandled && shortPressHandler != nullptr) {
            shortPressHandler();
        }
    }

    if (button.stablePressed && !button.longHandled &&
        now - button.pressedAtMs >= kLongPressMs) {
        button.longHandled = true;
        if (longPressHandler != nullptr) {
            longPressHandler();
        }
    }
}
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(kButtonPagePin, INPUT_PULLUP);
    pinMode(kButtonActionPin, INPUT_PULLUP);

    loadSettings();
    setupMode = readAndClearSetupNextFlag() || !upstreamConfigured() ||
                digitalRead(kButtonActionPin) == LOW;

    configureDisplay();
    startNetworks();
    configureWebServer();
    drawStatus();

    Serial.println(F("mobile-blocker guest gateway ready"));
}

void loop() {
    const uint32_t now = millis();

    dnsServer.processNextRequest();
    webServer.handleClient();
    expireGuestAccessIfNeeded();
    setGatewayOpen(!setupMode && guestAccessActive);
    maybeReconnectUpstream(now);
    updateButton(pageButton, kButtonPagePin, handlePageButtonShortPress, nullptr);
    updateButton(actionButton,
                 kButtonActionPin,
                 handleActionButtonShortPress,
                 handleActionButtonLongPress);
    maybeRefreshDisplay(now);
}
