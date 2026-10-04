// Bring-up sketch for the touch throttle: proves the display, touch and
// battery reading work from PlatformIO before the throttle UI is built on
// them (#6, "Check first"). Draws a crosshair under the finger and prints
// raw touch and battery readings to the serial console.

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

#include "config.h"

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
static Arduino_GFX *panel = new Arduino_AXS15231B(
    bus, LCD_RST, 0 /* rotation */, false /* IPS */, LCD_W, LCD_H,
    0, 0, 0, 0, axs15231b_180640_init_operations,
    sizeof(axs15231b_180640_init_operations));
// The AXS15231B needs whole-frame writes, so draw through a canvas.
static Arduino_Canvas *gfx = new Arduino_Canvas(LCD_W, LCD_H, panel, 0, 0, 0);

struct Touch {
    bool down;
    int16_t x, y;  // portrait pixels, origin top-left
};

static bool readTouch(Touch &t) {
    static const uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 0x0e, 0, 0, 0};
    uint8_t buf[32] = {0};
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(cmd, sizeof(cmd));
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom((int)TOUCH_ADDR, (int)sizeof(buf)) != (int)sizeof(buf)) return false;
    for (auto &b : buf) b = Wire.read();
    const int16_t rawLong  = ((buf[2] & 0x0f) << 8) | buf[3];  // 0..640
    const int16_t rawShort = ((buf[4] & 0x0f) << 8) | buf[5];  // 0..172
    t.down = buf[1] > 0 && buf[1] < 5;
    // Same mapping as Waveshare's LVGL demo for this orientation.
    t.x = constrain(rawShort, 0, LCD_W - 1);
    t.y = constrain(LCD_H - rawLong, 0, LCD_H - 1);
    return true;
}

static float batteryVolts() {
    return analogReadMilliVolts(BATT_ADC_PIN) * 0.001f * BATT_DIVIDER;
}

void setup() {
    Serial.begin(115200);
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    if (!gfx->begin()) Serial.println("display init failed");
    gfx->fillScreen(RGB565_BLACK);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(8, 8);
    gfx->print("touch349");
    gfx->flush();
    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
    analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);
}

void loop() {
    static uint32_t lastLog = 0;
    Touch t;
    if (readTouch(t) && t.down) {
        gfx->fillScreen(RGB565_BLACK);
        gfx->drawFastHLine(0, t.y, LCD_W, RGB565_GREEN);
        gfx->drawFastVLine(t.x, 0, LCD_H, RGB565_GREEN);
        gfx->setCursor(8, 8);
        gfx->printf("%d,%d", t.x, t.y);
        gfx->flush();
    }
    if (millis() - lastLog > 2000) {
        lastLog = millis();
        Serial.printf("battery %.2f V\n", batteryVolts());
    }
    delay(10);
}
