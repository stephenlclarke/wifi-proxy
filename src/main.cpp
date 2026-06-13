#include <Arduino.h>
#include <TFT_eSPI.h>

namespace {
constexpr uint32_t kHeartbeatIntervalMs = 1000;
constexpr int16_t kStatusY = 96;

TFT_eSPI display;
uint32_t lastHeartbeatMs = 0;
uint32_t heartbeatCount = 0;

void drawStaticStatus() {
    display.fillScreen(TFT_BLACK);
    display.setTextDatum(TL_DATUM);

    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.setTextFont(4);
    display.drawString("mobile-blocker", 12, 12);

    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextFont(2);
    display.drawString("LilyGo T-Display-S3", 12, 48);
    display.drawString("ESP32-S3 ready", 12, 68);
    display.drawString("Serial: 115200 baud", 12, 128);
}

void drawHeartbeat() {
    display.fillRect(12, kStatusY, 280, 20, TFT_BLACK);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setTextFont(2);
    display.drawString("Heartbeat: " + String(heartbeatCount), 12, kStatusY);
}
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

    display.init();
    display.setRotation(1);
    drawStaticStatus();
    drawHeartbeat();

    Serial.println("mobile-blocker firmware ready");
}

void loop() {
    const uint32_t now = millis();
    if (now - lastHeartbeatMs < kHeartbeatIntervalMs) {
        return;
    }

    lastHeartbeatMs = now;
    ++heartbeatCount;

    drawHeartbeat();
    Serial.printf("heartbeat=%lu uptime_ms=%lu\n",
                  static_cast<unsigned long>(heartbeatCount),
                  static_cast<unsigned long>(now));
}
