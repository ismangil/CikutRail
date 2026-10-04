// Touch throttle for JMRI's WiThrottle server (#6).
//
// Boot: hold BOOT to clear the stored Wi-Fi settings and run the setup portal.
// Running: BOOT is the e-stop. The main screen (Ui.h) drives the loco, fires the
// first three JMRI routes and presses three of the loco's functions; a long
// press on the status strip opens the loco picker.

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

#include <Preferences.h>
#include <WiFi.h>
#include <WiThrottleProtocol.h>

#include "AppDelegate.h"
#include "FunctionSlots.h"
#include "Link.h"
#include "Provision.h"
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

enum class Mode : uint8_t { Provisioning, Running, Picker };

static WiThrottleProtocol wit;
static AppDelegate        app;
static Link               jmri(wit, app);
static Preferences        prefs;
static Provision::Stored  cfg;
static Mode               mode = Mode::Running;

static Ui::Model model;
static int       fnNum[Ui::FUNCS] = {-1, -1, -1};  // function number per slot
static bool      locoChosen = false;   // acquire requested (or picker shown)
static bool      needDraw = true;
static String    shownStatus;

static const uint32_t LONG_PRESS_MS = 700;
static const uint32_t SPEED_SEND_MS = 40;

static String lastLoco() {
    prefs.begin(NVS_NAMESPACE, true);
    const String a = prefs.getString(NVS_KEY_LASTLOCO, "");
    prefs.end();
    return a;
}

static void saveLastLoco(const String &addr) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_LASTLOCO, addr);
    prefs.end();
}

static int rosterIndexOf(const String &addr) {
    for (size_t i = 0; i < app.roster.size(); i++)
        if (app.roster[i].witAddress() == addr) return (int)i;
    return -1;
}

static void sendSpeed() {
    if (!app.locoAcquired) return;
    wit.setDirection(THROTTLE_SLOT, model.forward ? Forward : Reverse);
    wit.setSpeed(THROTTLE_SLOT, abs(model.speed));
}

static void setSpeed(int v) {
    model.speed = Slider::clampSpeed(v);
    if (model.speed != 0) model.forward = model.speed > 0;
}

static void acquire(const String &addr) {
    if (app.locoAcquired && app.acquiredAddress == addr) return;
    if (app.locoAcquired) {
        // Stop the old loco before handing it back, so it does not keep
        // running at its last speed.
        wit.setSpeed(THROTTLE_SLOT, 0);
        wit.releaseLocomotive(THROTTLE_SLOT);
        app.locoAcquired = false;
        app.acquiredAddress = "";
    }
    setSpeed(0);
    if (wit.addLocomotive(THROTTLE_SLOT, addr)) saveLastLoco(addr);
}

// Pull what JMRI told us into the screen model.
static void syncModel() {
    const int ri = rosterIndexOf(app.acquiredAddress);
    if (ri >= 0) strlcpy(model.loco, app.roster[ri].name.c_str(), sizeof(model.loco));
    else strlcpy(model.loco, app.acquiredAddress.c_str(), sizeof(model.loco));

    const char *labels[MAX_FUNCTIONS];
    for (int i = 0; i < MAX_FUNCTIONS; i++) labels[i] = app.functionLabels[i].c_str();
    int slot[FunctionSlots::SLOTS];
    FunctionSlots::pick(labels, MAX_FUNCTIONS, slot);
    for (int i = 0; i < Ui::FUNCS; i++) {
        Ui::Fn &f = model.fn[i];
        fnNum[i] = slot[i];
        f.defined = slot[i] != FunctionSlots::NONE && app.locoAcquired;
        if (!f.defined) { f.on = false; f.label[0] = 0; continue; }
        strlcpy(f.label, labels[slot[i]], sizeof(f.label));
        f.momentary = FunctionSlots::looksMomentary(f.label);
        f.on = app.mirroredFunctions[slot[i]];
    }

    model.activeRoute = -1;
    for (int i = 0; i < Ui::ROUTES; i++) {
        if (i < (int)app.routes.size()) {
            const RouteEntry &r = app.routes[i];
            strlcpy(model.route[i], (r.userName.length() ? r.userName : r.sysName).c_str(),
                    sizeof(model.route[i]));
            if (r.state == RouteActive && model.activeRoute < 0) model.activeRoute = i;
        } else {
            model.route[i][0] = 0;
        }
    }

    // Do not fight the finger while the thumb is being dragged.
    if (model.pressed != Ui::HitThumb) {
        const int mag = app.mirroredSpeed;
        const bool fwd = app.mirroredDirection == Forward;
        model.speed = Slider::clampSpeed(fwd ? mag : -mag);
        model.forward = fwd;
    }
    model.linkOk = jmri.online();
}

static void openPicker() {
    if (app.roster.empty()) return;
    mode = Mode::Picker;
    String names[Ui::PICK_ROWS];
    int n = (int)app.roster.size();
    if (n > Ui::PICK_ROWS) n = Ui::PICK_ROWS;
    for (int i = 0; i < n; i++) names[i] = app.roster[i].name;
    Ui::picker(gfx, names, (int)app.roster.size(), rosterIndexOf(app.acquiredAddress));
    gfx->flush();
}

static void showMessage(const char *title, const String &body) {
    Ui::message(gfx, title, body);
    gfx->flush();
}

static void handlePickerTouch(const Touch &t) {
    static bool wasDown = false;
    if (!t.down && wasDown) {
        const int hit = Ui::pickerHit(t.x, t.y, (int)app.roster.size());
        if (hit == Ui::PICK_CANCEL) {
            mode = Mode::Running;
            needDraw = true;
        } else if (hit >= 0) {
            acquire(app.roster[hit].witAddress());
            mode = Mode::Running;
            needDraw = true;
        }
    }
    wasDown = t.down;
}

// Touch state machine for the main screen. A press picks what it landed on;
// only the thumb follows the finger, so a slip elsewhere cannot change speed.
static void handleTouch(const Touch &t) {
    static bool     wasDown = false;
    static int      grabOffset = 0;
    static uint32_t pressAt = 0, lastSent = 0;
    static bool     pickerTriggered = false;
    bool changed = false;

    if (t.down && !wasDown) {
        const int hit = Ui::onThumb(model, t.x, t.y) ? Ui::HitThumb : Ui::hitTest(t.x, t.y);
        model.pressed = hit;
        pressAt = millis();
        pickerTriggered = false;
        changed = true;
        if (hit == Ui::HitThumb) {
            grabOffset = t.y - Ui::thumbCentreY(model.speed);
        } else if (hit == Ui::HitIdle) {
            setSpeed(0);
            sendSpeed();
        } else if (hit >= Ui::HitFn0 && hit < Ui::HitFn0 + Ui::FUNCS) {
            const int i = hit - Ui::HitFn0;
            // Press and release go to JMRI as they happen; JMRI decides from
            // the roster whether the function latches or is momentary.
            if (model.fn[i].defined && app.locoAcquired)
                wit.setFunction(THROTTLE_SLOT, fnNum[i], true);
        }
    } else if (t.down && wasDown) {
        if (model.pressed == Ui::HitThumb) {
            const int v = Slider::speedFromY(t.y - grabOffset, Ui::sliderTop(),
                                             Ui::sliderBottom());
            if (v != model.speed) {
                setSpeed(v);
                changed = true;
                if (millis() - lastSent >= SPEED_SEND_MS) {
                    lastSent = millis();
                    sendSpeed();
                }
            }
        } else if (model.pressed == Ui::HitStatus && !pickerTriggered &&
                   millis() - pressAt > LONG_PRESS_MS) {
            pickerTriggered = true;
            model.pressed = Ui::HitNone;
            wasDown = true;
            openPicker();
            return;
        }
    } else if (!t.down && wasDown) {
        const int hit = model.pressed;
        const bool same = Ui::hitTest(t.x, t.y) == hit;
        if (hit == Ui::HitThumb) {
            sendSpeed();  // final value
        } else if (hit >= Ui::HitRoute0 && hit < Ui::HitRoute0 + Ui::ROUTES && same) {
            const int i = hit - Ui::HitRoute0;
            if (i < (int)app.routes.size() && jmri.online())
                wit.setRoute(app.routes[i].sysName);
        } else if (hit >= Ui::HitFn0 && hit < Ui::HitFn0 + Ui::FUNCS) {
            const int i = hit - Ui::HitFn0;
            if (model.fn[i].defined && app.locoAcquired)
                wit.setFunction(THROTTLE_SLOT, fnNum[i], false);
        }
        model.pressed = Ui::HitNone;
        changed = true;
    }
    wasDown = t.down;
    if (changed) needDraw = true;
}

static void checkEstop() {
    static bool wasLow = false;
    const bool low = digitalRead(BOOT_BUTTON_PIN) == LOW;
    if (low && !wasLow && app.locoAcquired) {
        wit.emergencyStop(THROTTLE_SLOT);
        setSpeed(0);
        needDraw = true;
    }
    wasLow = low;
}

static String deviceName() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char tail[5];
    snprintf(tail, sizeof(tail), "%02X%02X", mac[4], mac[5]);
    return String("Touch349-") + tail;
}

static void startProvisioning() {
    mode = Mode::Provisioning;
    String ssid;
    IPAddress ip;
    Provision::begin(ssid, ip);
    showMessage("Wi-Fi setup", "Join the Wi-Fi network\n\n  " + ssid +
                "\n\nthen open\n\n  http://" + ip.toString() + "/");
}

void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    if (!gfx->begin()) Serial.println("display init failed");
    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
    analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);
    model.battPct = batteryPercent();

    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {   // BOOT held at power-on
        Provision::clearAll();
        cfg = Provision::Stored{};
    } else {
        Provision::load(cfg);
    }
    if (cfg.ssid.length() == 0) {
        startProvisioning();
    } else {
        jmri.begin(cfg, deviceName());
    }
}

void loop() {
    static uint32_t lastBatt = 0;
    Touch t;
    const bool haveTouch = readTouch(t);

    if (mode == Mode::Provisioning) {
        Provision::tick();
        if (Provision::isDone()) {
            Provision::end();
            showMessage("Saved", "Restarting...");
            delay(300);
            ESP.restart();
        }
        delay(5);
        return;
    }

    wit.check();
    jmri.tick();
    checkEstop();

    if (jmri.state() == Link::State::NeedsSetup) {
        startProvisioning();
        return;
    }

    if (jmri.takeOnline()) {
        // Re-acquire after a reconnect; on first connect pick the last loco.
        if (app.locoAcquired && app.acquiredAddress.length()) {
            wit.addLocomotive(THROTTLE_SLOT, app.acquiredAddress);
        } else {
            locoChosen = false;
        }
    }

    if (!jmri.online()) {
        // Show the link status until we are back.
        if (jmri.statusText() != shownStatus) {
            shownStatus = jmri.statusText();
            showMessage("JMRI", shownStatus);
        }
        delay(10);
        return;
    }
    if (shownStatus.length()) {   // just came back online
        shownStatus = "";
        needDraw = true;
    }

    if (!locoChosen && app.rosterPopulated) {
        locoChosen = true;
        const String last = lastLoco();
        if (last.length() && rosterIndexOf(last) >= 0) acquire(last);
        else openPicker();
    }

    if (mode == Mode::Picker) {
        if (haveTouch) handlePickerTouch(t);
        delay(10);
        return;
    }

    if (haveTouch) handleTouch(t);

    if (app.dirty) {
        app.dirty = false;
        needDraw = true;
    }
    if (millis() - lastBatt > 10000) {
        lastBatt = millis();
        model.battPct = batteryPercent();
        needDraw = true;
    }
    if (needDraw) {
        needDraw = false;
        syncModel();
        Ui::draw(gfx, model);
        gfx->flush();
    }
    delay(10);
}
