// Main throttle screen for the 172 x 640 portrait display.
//
//   status strip   loco, link, battery
//   3 route buttons (the sidings)
//   3 function buttons
//   centre-zero speed slider (touch the thumb and drag)
//   IDLE button
//
// Drawing and hit-testing only; the touch state machine is in main.cpp.

#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

namespace Ui {

constexpr int ROUTES = 3;
constexpr int FUNCS  = 3;

struct Fn {
    char label[12] = "";
    bool defined   = false;
    bool on        = false;
    bool momentary = false;
};

struct Model {
    char loco[16]  = "";
    int  speed     = 0;       // signed, -126..126
    bool forward   = true;    // last direction; kept while speed is 0
    Fn   fn[FUNCS];
    char route[ROUTES][16] = {"", "", ""};
    int  activeRoute = -1;    // lit route, or -1
    int  pressed     = -1;    // hit id currently held, or -1
    int  battPct     = -1;    // -1 = unknown
    bool linkOk      = false;
};

enum Hit : int {
    HitNone = -1,
    HitRoute0 = 0,                       // .. HitRoute0 + ROUTES - 1
    HitFn0 = ROUTES,                     // .. HitFn0 + FUNCS - 1
    HitThumb = ROUTES + FUNCS,
    HitIdle,
    HitStatus,
};

// Slider track: the thumb centre travels between these y values.
int sliderTop();
int sliderBottom();
int thumbCentreY(int speed);

Hit hitTest(int x, int y);
bool onThumb(const Model &m, int x, int y);

void draw(Arduino_GFX *g, const Model &m);

}  // namespace Ui
