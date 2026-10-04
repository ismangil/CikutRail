#include "Ui.h"

#include "Speed.h"
#include "config.h"

namespace Ui {

namespace {

struct Rect { int16_t x, y, w, h; };

// Vertical layout, top to bottom (172 x 640).
constexpr Rect STATUS   = {0, 0, LCD_W, 28};
constexpr int  ROUTE_Y0 = 34, ROUTE_H = 60, ROUTE_GAP = 4;
constexpr int  FN_Y = 232, FN_H = 56, FN_W = 54, FN_GAP = 5;
constexpr Rect UP       = {4, 298, LCD_W - 8, 84};
constexpr Rect SPEED    = {4, 386, LCD_W - 8, 86};
constexpr Rect DOWN     = {4, 476, LCD_W - 8, 84 - 8};
constexpr Rect IDLE     = {4, 560, LCD_W - 8, 76};

constexpr Rect routeRect(int i) {
    return {4, (int16_t)(ROUTE_Y0 + i * (ROUTE_H + ROUTE_GAP)), LCD_W - 8, ROUTE_H};
}
constexpr Rect fnRect(int i) {
    return {(int16_t)(i * (FN_W + FN_GAP)), FN_Y, FN_W, FN_H};
}

bool inside(const Rect &r, int x, int y, int pad = 0) {
    return x >= r.x - pad && x < r.x + r.w + pad && y >= r.y - pad && y < r.y + r.h + pad;
}

uint16_t c(Arduino_GFX *g, uint8_t r, uint8_t gr, uint8_t b) {
    return g->color565(r, gr, b);
}

void centredText(Arduino_GFX *g, const Rect &r, const char *s, uint8_t size,
                 uint16_t col) {
    g->setTextSize(size);
    g->setTextColor(col);
    const int w = (int)strlen(s) * 6 * size;
    g->setCursor(r.x + (r.w - w) / 2, r.y + (r.h - 8 * size) / 2);
    g->print(s);
}

void button(Arduino_GFX *g, const Rect &r, const char *s, uint8_t size,
            uint16_t fill, uint16_t text, bool pressed) {
    const uint16_t edge = c(g, 90, 90, 90);
    g->fillRoundRect(r.x, r.y, r.w, r.h, 8, pressed ? edge : fill);
    g->drawRoundRect(r.x, r.y, r.w, r.h, 8, edge);
    centredText(g, r, s, size, text);
}

// Filled triangle on a button: up or down.
void arrow(Arduino_GFX *g, const Rect &r, bool up, uint16_t fill, bool pressed) {
    const uint16_t edge = c(g, 90, 90, 90);
    g->fillRoundRect(r.x, r.y, r.w, r.h, 8, pressed ? edge : fill);
    g->drawRoundRect(r.x, r.y, r.w, r.h, 8, edge);
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    const int hw = 34, hh = 18;
    if (up) g->fillTriangle(cx, cy - hh, cx - hw, cy + hh, cx + hw, cy + hh, RGB565_WHITE);
    else g->fillTriangle(cx, cy + hh, cx - hw, cy - hh, cx + hw, cy - hh, RGB565_WHITE);
}

}  // namespace

Hit hitTest(int x, int y) {
    for (int i = 0; i < ROUTES; i++)
        if (inside(routeRect(i), x, y)) return (Hit)(HitRoute0 + i);
    for (int i = 0; i < FUNCS; i++)
        if (inside(fnRect(i), x, y)) return (Hit)(HitFn0 + i);
    if (inside(UP, x, y)) return HitUp;
    if (inside(DOWN, x, y)) return HitDown;
    if (inside(IDLE, x, y)) return HitIdle;
    if (inside(STATUS, x, y)) return HitStatus;
    return HitNone;
}

void draw(Arduino_GFX *g, const Model &m) {
    const uint16_t bg    = c(g, 8, 10, 14);
    const uint16_t panel = c(g, 34, 38, 46);
    const uint16_t amber = c(g, 255, 176, 0);
    const uint16_t green = c(g, 40, 200, 90);
    const uint16_t red   = c(g, 220, 60, 50);
    const uint16_t blue  = c(g, 70, 150, 255);   // reverse
    const uint16_t white = RGB565_WHITE;
    const uint16_t grey  = c(g, 140, 140, 140);

    g->fillScreen(bg);

    // Status strip
    g->setTextSize(2);
    // Loco name doubles as the link indicator: green online, red struck out offline.
    const char *name = m.loco[0] ? m.loco : "no loco";
    g->setTextColor(m.linkOk ? green : red);
    g->setCursor(4, 6);
    g->print(name);
    if (!m.linkOk) g->drawFastHLine(4, 13, (int)strlen(name) * 12, red);
    char b[12];
    if (m.battPct >= 0) snprintf(b, sizeof(b), "%d%%", m.battPct);
    else strcpy(b, "--");
    g->setTextColor(m.battPct >= 0 && m.battPct < 20 ? red : grey);
    g->setCursor(LCD_W - 4 - (int)strlen(b) * 12, 6);
    g->print(b);

    // Routes
    for (int i = 0; i < ROUTES; i++) {
        const bool lit = (m.activeRoute == i);
        button(g, routeRect(i), m.route[i][0] ? m.route[i] : "-", 2,
               lit ? amber : panel, lit ? bg : white, m.pressed == HitRoute0 + i);
    }

    // Functions
    for (int i = 0; i < FUNCS; i++) {
        const Fn &f = m.fn[i];
        const Rect r = fnRect(i);
        if (!f.defined) {
            g->drawRoundRect(r.x, r.y, r.w, r.h, 8, c(g, 40, 40, 40));
            continue;
        }
        button(g, r, f.label, 1, f.on ? green : panel, f.on ? bg : white,
               m.pressed == HitFn0 + i);
    }

    // Speed: up / down arrows around the signed speed
    const uint16_t sc = m.speed == 0 ? grey : (m.speed > 0 ? green : blue);
    arrow(g, UP, true, panel, m.pressed == HitUp);
    arrow(g, DOWN, false, panel, m.pressed == HitDown);
    char n[12];
    snprintf(n, sizeof(n), "%d", m.speed);
    centredText(g, SPEED, n, 5, sc);

    // IDLE
    button(g, IDLE, "IDLE", 3, red, white, m.pressed == HitIdle);
}

void message(Arduino_GFX *g, const char *title, const String &body) {
    g->fillScreen(g->color565(8, 10, 14));
    g->setTextColor(g->color565(255, 176, 0));
    g->setTextSize(2);
    g->setCursor(4, 12);
    g->print(title);
    g->setTextColor(RGB565_WHITE);
    g->setTextSize(1);
    g->setCursor(4, 56);
    g->print(body);
}

namespace {
constexpr int PICK_Y0 = 8, PICK_H = 60, PICK_GAP = 4;
Rect pickRect(int i) {
    return {4, (int16_t)(PICK_Y0 + i * (PICK_H + PICK_GAP)), LCD_W - 8, PICK_H};
}
}  // namespace

void picker(Arduino_GFX *g, const String *names, int n, int current) {
    g->fillScreen(g->color565(8, 10, 14));
    const int rows = n < PICK_ROWS ? n : PICK_ROWS;
    for (int i = 0; i < rows; i++) {
        const bool cur = (i == current);
        char b[16];
        strlcpy(b, names[i].c_str(), sizeof(b));
        button(g, pickRect(i), b, 2, cur ? g->color565(40, 200, 90) : g->color565(34, 38, 46),
               cur ? g->color565(8, 10, 14) : RGB565_WHITE, false);
    }
    button(g, pickRect(PICK_ROWS), "Cancel", 2, g->color565(60, 60, 60), RGB565_WHITE, false);
}

int pickerHit(int x, int y, int n) {
    const int rows = n < PICK_ROWS ? n : PICK_ROWS;
    for (int i = 0; i < rows; i++)
        if (inside(pickRect(i), x, y)) return i;
    if (inside(pickRect(PICK_ROWS), x, y)) return PICK_CANCEL;
    return -1;
}

}  // namespace Ui
