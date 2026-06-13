#include <Arduino.h>
#include <DNSServer.h>
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
#define PORTAL_AP_SSID "mobile-blocker"
#endif

#ifndef PORTAL_AP_PASSWORD
#define PORTAL_AP_PASSWORD "change-me-1234"
#endif

namespace {
constexpr byte kDnsPort = 53;
constexpr uint16_t kHttpPort = 80;
constexpr uint32_t kDisplayRefreshMs = 1000;
constexpr uint32_t kWifiReconnectMs = 10000;
constexpr uint8_t kBoardPowerEnablePin = 15;
const IPAddress kPortalIp(192, 168, 42, 1);
const IPAddress kPortalGateway(192, 168, 42, 1);
const IPAddress kPortalSubnet(255, 255, 255, 0);

TFT_eSPI display;
DNSServer dnsServer;
WebServer webServer(kHttpPort);

uint32_t lastDisplayRefreshMs = 0;
uint32_t lastWifiReconnectMs = 0;
bool naptEnabled = false;

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

bool upstreamConfigured() {
    return String(WIFI_UPSTREAM_SSID).length() > 0;
}

bool upstreamConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String upstreamIpText() {
    if (!upstreamConnected()) {
        return F("not connected");
    }

    return WiFi.localIP().toString();
}

String gatewayModeText() {
#if MOBILE_BLOCKER_NAPT_AVAILABLE
    return naptEnabled ? F("NAT gateway enabled") : F("NAT available, not enabled");
#else
    return F("portal only; NAT not enabled in Arduino SDK");
#endif
}

String statusJson() {
    String body;
    body.reserve(320);
    body += F("{\"apSsid\":\"");
    body += jsonEscape(PORTAL_AP_SSID);
    body += F("\",\"portalIp\":\"");
    body += WiFi.softAPIP().toString();
    body += F("\",\"clients\":");
    body += WiFi.softAPgetStationNum();
    body += F(",\"upstreamConfigured\":");
    body += upstreamConfigured() ? F("true") : F("false");
    body += F(",\"upstreamConnected\":");
    body += upstreamConnected() ? F("true") : F("false");
    body += F(",\"upstreamIp\":\"");
    body += jsonEscape(upstreamIpText());
    body += F("\",\"gatewayMode\":\"");
    body += jsonEscape(gatewayModeText());
    body += F("\"}");
    return body;
}

String portalHtml() {
    String body;
    body.reserve(2600);
    body += F("<!doctype html><html lang=\"en\"><head>"
              "<meta charset=\"utf-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
              "<title>mobile-blocker portal</title>"
              "<style>"
              ":root{color-scheme:light dark;font-family:-apple-system,BlinkMacSystemFont,"
              "\"Segoe UI\",sans-serif;background:#111;color:#f5f5f5}"
              "body{margin:0;padding:28px;line-height:1.45}"
              "main{max-width:680px;margin:0 auto}"
              "h1{font-size:1.65rem;margin:0 0 16px}"
              ".panel{border:1px solid #3b3b3b;border-radius:8px;padding:18px;margin:14px 0;"
              "background:#1b1b1b}"
              "dl{display:grid;grid-template-columns:max-content 1fr;gap:8px 16px;margin:0}"
              "dt{color:#b6b6b6}dd{margin:0;word-break:break-word}"
              ".ok{color:#51d88a}.warn{color:#ffcf5c}"
              "button{appearance:none;border:0;border-radius:6px;padding:12px 16px;"
              "background:#2f80ed;color:white;font-weight:700;font-size:1rem}"
              "p{margin:0 0 12px}"
              "</style></head><body><main>");
    body += F("<h1>mobile-blocker portal</h1>");
    body += F("<section class=\"panel\"><p>This portal is for users of this local network. "
              "It does not collect Wi-Fi passwords or inspect encrypted traffic.</p>");
    body += F("<form method=\"post\" action=\"/accept\"><button type=\"submit\">Continue</button>"
              "</form></section>");
    body += F("<section class=\"panel\"><dl>");
    body += F("<dt>Portal SSID</dt><dd>");
    body += htmlEscape(PORTAL_AP_SSID);
    body += F("</dd><dt>Portal IP</dt><dd>");
    body += WiFi.softAPIP().toString();
    body += F("</dd><dt>Connected clients</dt><dd>");
    body += WiFi.softAPgetStationNum();
    body += F("</dd><dt>Upstream SSID</dt><dd>");
    body += upstreamConfigured() ? htmlEscape(WIFI_UPSTREAM_SSID) : F("not configured");
    body += F("</dd><dt>Upstream status</dt><dd class=\"");
    body += upstreamConnected() ? F("ok\">connected") : F("warn\">not connected");
    body += F("</dd><dt>Upstream IP</dt><dd>");
    body += upstreamIpText();
    body += F("</dd><dt>Gateway mode</dt><dd>");
    body += gatewayModeText();
    body += F("</dd></dl></section>");
    body += F("</main></body></html>");
    return body;
}

void sendPortal() {
    webServer.sendHeader(F("Cache-Control"), F("no-store"));
    webServer.send(200, F("text/html"), portalHtml());
}

void sendCaptiveProbeSuccess() {
    webServer.sendHeader(F("Cache-Control"), F("no-store"));
    webServer.send(200, F("text/plain"), F("Success"));
}

void handleAccept() {
    webServer.sendHeader(F("Location"), F("/"));
    webServer.send(303);
}

void configureWebServer() {
    webServer.on(F("/"), HTTP_GET, sendPortal);
    webServer.on(F("/accept"), HTTP_POST, handleAccept);
    webServer.on(F("/status"), HTTP_GET, []() {
        webServer.sendHeader(F("Cache-Control"), F("no-store"));
        webServer.send(200, F("application/json"), statusJson());
    });

    // Common captive-network probes for Android, Apple, Windows, and Firefox.
    webServer.on(F("/generate_204"), HTTP_GET, sendPortal);
    webServer.on(F("/gen_204"), HTTP_GET, sendPortal);
    webServer.on(F("/hotspot-detect.html"), HTTP_GET, sendPortal);
    webServer.on(F("/library/test/success.html"), HTTP_GET, sendPortal);
    webServer.on(F("/connecttest.txt"), HTTP_GET, sendPortal);
    webServer.on(F("/ncsi.txt"), HTTP_GET, sendCaptiveProbeSuccess);
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

void drawStatus() {
    display.fillScreen(TFT_BLACK);

    display.setTextFont(4);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.drawString(F("Wi-Fi portal"), 10, 8);

    display.setTextFont(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString(String(F("AP: ")) + PORTAL_AP_SSID, 10, 42);
    display.drawString(String(F("Portal: ")) + WiFi.softAPIP().toString(), 10, 62);
    display.drawString(String(F("Clients: ")) + WiFi.softAPgetStationNum(), 10, 82);

    display.setTextColor(upstreamConnected() ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    display.drawString(upstreamConnected() ? F("Upstream: connected") : F("Upstream: offline"),
                       10,
                       104);

    display.setTextColor(naptEnabled ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    display.drawString(naptEnabled ? F("Gateway: NAT") : F("Gateway: portal only"), 10, 126);
}

void connectUpstream() {
    if (!upstreamConfigured()) {
        Serial.println(F("No upstream SSID configured; running captive portal only"));
        return;
    }

    if (upstreamConnected() || WiFi.status() == WL_CONNECT_FAILED ||
        WiFi.status() == WL_IDLE_STATUS) {
        WiFi.disconnect(false, false);
    }

    Serial.printf("Connecting upstream SSID '%s'\n", WIFI_UPSTREAM_SSID);
    WiFi.begin(WIFI_UPSTREAM_SSID, WIFI_UPSTREAM_PASSWORD);
}

void startNetworks() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(kPortalIp, kPortalGateway, kPortalSubnet);

    const bool apStarted = WiFi.softAP(PORTAL_AP_SSID, PORTAL_AP_PASSWORD);
    Serial.printf("Portal AP %s: %s (%s)\n",
                  apStarted ? "started" : "failed",
                  PORTAL_AP_SSID,
                  WiFi.softAPIP().toString().c_str());

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(kDnsPort, F("*"), WiFi.softAPIP());

    connectUpstream();
}

void enableGatewayIfSupported() {
    if (!upstreamConnected() || naptEnabled) {
        return;
    }

#if MOBILE_BLOCKER_NAPT_AVAILABLE
    const int result = ip_napt_enable(static_cast<uint32_t>(WiFi.softAPIP()), 1);
    naptEnabled = result == 0;
    Serial.printf("NAPT enable result=%d\n", result);
#else
    naptEnabled = false;
    Serial.println(F("NAPT unavailable in this Arduino ESP32-S3 SDK build"));
#endif
}

void maybeReconnectUpstream(uint32_t now) {
    if (!upstreamConfigured() || upstreamConnected()) {
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
    Serial.printf("portal_ip=%s clients=%d upstream=%s upstream_ip=%s gateway=%s\n",
                  WiFi.softAPIP().toString().c_str(),
                  WiFi.softAPgetStationNum(),
                  upstreamConnected() ? "connected" : "offline",
                  upstreamIpText().c_str(),
                  gatewayModeText().c_str());
}
}

void setup() {
    Serial.begin(115200);
    delay(200);

    configureDisplay();
    startNetworks();
    configureWebServer();
    drawStatus();

    Serial.println(F("mobile-blocker captive portal ready"));
}

void loop() {
    const uint32_t now = millis();

    dnsServer.processNextRequest();
    webServer.handleClient();
    enableGatewayIfSupported();
    maybeReconnectUpstream(now);
    maybeRefreshDisplay(now);
}
