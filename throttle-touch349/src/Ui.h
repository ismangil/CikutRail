// Main throttle screen for the 172 x 640 portrait display.
//
//   status strip   loco, link, battery
//   3 route buttons (the sidings)
//   3 function buttons
//   up / down arrows around the signed speed (green forward, blue reverse)
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
    HitUp = ROUTES + FUNCS,
    HitDown,
    HitIdle,
    HitStatus,
};

Hit hitTest(int x, int y);

void draw(Arduino_GFX *g, const Model &m);

// Full-screen text for setup and connection progress.
void message(Arduino_GFX *g, const char *title, const String &body);

// Loco picker: one row per roster entry (the first PICK_ROWS), plus Cancel.
constexpr int PICK_ROWS = 8;
constexpr int PICK_CANCEL = -2;
void picker(Arduino_GFX *g, const String *names, int n, int current);
// Row index for a touch, PICK_CANCEL for the Cancel row, -1 for nothing.
int pickerHit(int x, int y, int n);

}  // namespace Ui
