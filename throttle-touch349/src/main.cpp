// Touch throttle, UI mock (#6). Draws the main screen and handles touch, with
// a fake loco and routes standing in for JMRI: nothing is sent anywhere yet.
// Speed and button events are logged to the serial console.

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

#include "FunctionSlots.h"
#include "Slider.h"
#include "Ui.h"
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

// Rough LiPo state of charge from the resting voltage.
static int batteryPercent() {
    const float v = batteryVolts();
    return constrain((int)((v - 3.3f) / (4.15f - 3.3f) * 100.0f), 0, 100);
}

static Ui::Model model;

// Stand-in roster entry: the GP40 has lights and uncoupling.
static void loadMockLoco() {
    strcpy(model.loco, "GP40");
    const char *labels[13] = {"Light", "", "", "", "", "", "", "", "", "",
                              "Uncouple", "", ""};
    int slot[FunctionSlots::SLOTS];
    FunctionSlots::pick(labels, 13, slot);
    for (int i = 0; i < Ui::FUNCS; i++) {
        Ui::Fn &f = model.fn[i];
        f.defined = slot[i] != FunctionSlots::NONE;
        if (!f.defined) continue;
        strlcpy(f.label, labels[slot[i]], sizeof(f.label));
        f.momentary = FunctionSlots::looksMomentary(f.label);
    }
    strcpy(model.route[0], "Siding 1");
    strcpy(model.route[1], "Siding 2");
    strcpy(model.route[2], "Siding 3");
    model.linkOk = true;
}

static void setSpeed(int v) {
    model.speed = Slider::clampSpeed(v);
    if (model.speed != 0) model.forward = model.speed > 0;
}

// Touch state machine. A press picks what it landed on; only the thumb
// follows the finger, so a slip elsewhere cannot change speed.
static void handleTouch(const Touch &t) {
    static bool wasDown = false;
    static int  grabOffset = 0;
    static int  lastLogged = 0;
    bool changed = false;

    if (t.down && !wasDown) {
        int hit = Ui::onThumb(model, t.x, t.y) ? Ui::HitThumb : Ui::hitTest(t.x, t.y);
        model.pressed = hit;
        changed = true;
        if (hit == Ui::HitThumb) {
            grabOffset = t.y - Ui::thumbCentreY(model.speed);
        } else if (hit == Ui::HitIdle) {
            setSpeed(0);
        } else if (hit >= Ui::HitFn0 && hit < Ui::HitFn0 + Ui::FUNCS) {
            Ui::Fn &f = model.fn[hit - Ui::HitFn0];
            if (f.defined && f.momentary) {
                f.on = true;
                Serial.printf("fn %s down\n", f.label);
            }
        }
    } else if (t.down && wasDown && model.pressed == Ui::HitThumb) {
        const int v = Slider::speedFromY(t.y - grabOffset, Ui::sliderTop(),
                                         Ui::sliderBottom());
        if (v != model.speed) {
            setSpeed(v);
            changed = true;
        }
    } else if (!t.down && wasDown) {
        const int hit = model.pressed;
        // Release over the same button completes a tap.
        const bool same = Ui::hitTest(t.x, t.y) == hit;
        if (hit >= Ui::HitRoute0 && hit < Ui::HitRoute0 + Ui::ROUTES && same) {
            model.activeRoute = hit - Ui::HitRoute0;
            Serial.printf("route %s\n", model.route[model.activeRoute]);
        } else if (hit >= Ui::HitFn0 && hit < Ui::HitFn0 + Ui::FUNCS) {
            Ui::Fn &f = model.fn[hit - Ui::HitFn0];
            if (f.defined && f.momentary) {
                f.on = false;
                Serial.printf("fn %s up\n", f.label);
            } else if (f.defined && same) {
                f.on = !f.on;
                Serial.printf("fn %s %s\n", f.label, f.on ? "on" : "off");
            }
        }
        model.pressed = Ui::HitNone;
        changed = true;
    }
    wasDown = t.down;

    if (model.speed != lastLogged) {
        lastLogged = model.speed;
        Serial.printf("speed %d\n", model.speed);
    }
    if (changed) {
        Ui::draw(gfx, model);
        gfx->flush();
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    if (!gfx->begin()) Serial.println("display init failed");
    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
    analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);
    loadMockLoco();
    model.battPct = batteryPercent();
    Ui::draw(gfx, model);
    gfx->flush();
}

void loop() {
    static uint32_t lastBatt = 0;
    Touch t;
    if (readTouch(t)) handleTouch(t);
    if (millis() - lastBatt > 10000) {
        lastBatt = millis();
        model.battPct = batteryPercent();
        Serial.printf("battery %.2f V\n", batteryVolts());
        if (model.pressed == Ui::HitNone) {
            Ui::draw(gfx, model);
            gfx->flush();
        }
    }
    delay(10);
}
