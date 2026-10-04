// Centre-zero throttle slider maths. Plain C++ so it runs in native tests.
//
// The track runs from yFwd (top, full forward) to yRev (bottom, full
// reverse); the middle is zero. Positions are thumb-centre y coordinates.

#pragma once

#include <stdint.h>

namespace Slider {

constexpr int MAX_SPEED = 126;
// Thumb centres this close to the middle (in speed steps) snap to zero.
constexpr int SNAP = 5;

inline int clampSpeed(int v) {
    return v > MAX_SPEED ? MAX_SPEED : (v < -MAX_SPEED ? -MAX_SPEED : v);
}

// Signed speed for a thumb centre at y. Positive = forward (up).
inline int speedFromY(int y, int yFwd, int yRev) {
    const int mid = (yFwd + yRev) / 2;
    const int half = (yRev - yFwd) / 2;
    if (half <= 0) return 0;
    const int v = clampSpeed(((mid - y) * MAX_SPEED) / half);
    return (v >= -SNAP && v <= SNAP) ? 0 : v;
}

inline int yFromSpeed(int speed, int yFwd, int yRev) {
    const int mid = (yFwd + yRev) / 2;
    const int half = (yRev - yFwd) / 2;
    return mid - (clampSpeed(speed) * half) / MAX_SPEED;
}

}  // namespace Slider
