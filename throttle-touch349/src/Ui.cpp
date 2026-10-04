#include "Ui.h"

#include "Slider.h"
#include "config.h"

namespace Ui {

namespace {

struct Rect { int16_t x, y, w, h; };

// Vertical layout, top to bottom (172 x 640).
constexpr Rect STATUS   = {0, 0, LCD_W, 28};
constexpr int  ROUTE_Y0 = 34, ROUTE_H = 60, ROUTE_GAP = 4;
constexpr int  FN_Y = 232, FN_H = 56, FN_W = 54, FN_GAP = 5;
constexpr Rect SLIDER   = {0, 298, LCD_W, 254};
constexpr Rect IDLE     = {4, 560, LCD_W - 8, 76};
constexpr int  THUMB_W = 96, THUMB_H = 56;
constexpr int  THUMB_PAD = 10;   // extra grab area around the thumb

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

}  // namespace

int sliderTop()    { return SLIDER.y + 14 + THUMB_H / 2 + 20; }
int sliderBottom() { return SLIDER.y + SLIDER.h - 14 - THUMB_H / 2 - 20; }
int thumbCentreY(int speed) {
    return Slider::yFromSpeed(speed, sliderTop(), sliderBottom());
}

Hit hitTest(int x, int y) {
    for (int i = 0; i < ROUTES; i++)
        if (inside(routeRect(i), x, y)) return (Hit)(HitRoute0 + i);
    for (int i = 0; i < FUNCS; i++)
        if (inside(fnRect(i), x, y)) return (Hit)(HitFn0 + i);
    if (inside(IDLE, x, y)) return HitIdle;
    if (inside(STATUS, x, y)) return HitStatus;
    return HitNone;
}

// The thumb needs the current speed, so it has its own test.
bool onThumb(const Model &m, int x, int y) {
    const int cy = thumbCentreY(m.speed);
    const Rect t = {(int16_t)((LCD_W - THUMB_W) / 2), (int16_t)(cy - THUMB_H / 2),
                    THUMB_W, THUMB_H};
    return inside(t, x, y, THUMB_PAD);
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
    g->setTextColor(white);
    g->setCursor(4, 6);
    g->print(m.loco[0] ? m.loco : "no loco");
    char b[12];
    if (m.battPct >= 0) snprintf(b, sizeof(b), "%d%%", m.battPct);
    else strcpy(b, "--");
    g->setTextColor(m.battPct >= 0 && m.battPct < 20 ? red : grey);
    g->setCursor(LCD_W - 4 - (int)strlen(b) * 12, 6);
    g->print(b);
    g->fillCircle(LCD_W / 2 + 14, 14, 5, m.linkOk ? green : red);

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

    // Slider: track, FWD / REV labels, thumb, speed number
    const int cx = LCD_W / 2;
    const int top = sliderTop(), bot = sliderBottom(), mid = (top + bot) / 2;
    g->setTextSize(1);
    g->setTextColor(grey);
    g->setCursor(cx - 9, SLIDER.y + 2);
    g->print("FWD");
    g->setCursor(cx - 9, SLIDER.y + SLIDER.h - 10);
    g->print("REV");
    g->fillRoundRect(cx - 4, top, 8, bot - top, 4, panel);
    g->drawFastHLine(cx - 40, mid, 80, grey);   // zero mark
    const int ty = thumbCentreY(m.speed);
    const uint16_t tc = m.speed == 0 ? grey : (m.speed > 0 ? green : blue);
    g->fillRoundRect((LCD_W - THUMB_W) / 2, ty - THUMB_H / 2, THUMB_W, THUMB_H, 10,
                     m.pressed == HitThumb ? white : tc);
    char n[12];
    snprintf(n, sizeof(n), "%d", m.speed < 0 ? -m.speed : m.speed);
    const Rect tr = {(int16_t)((LCD_W - THUMB_W) / 2), (int16_t)(ty - THUMB_H / 2),
                     THUMB_W, THUMB_H};
    centredText(g, tr, n, 4, bg);

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
