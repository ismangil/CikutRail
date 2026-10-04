// Signed throttle speed maths. Plain C++ so it runs in native tests.
// Positive = forward, negative = reverse.

#pragma once

namespace Speed {

constexpr int MAX_SPEED = 126;

inline int clamp(int v) {
    return v > MAX_SPEED ? MAX_SPEED : (v < -MAX_SPEED ? -MAX_SPEED : v);
}

// One arrow press: dir is +1 (up) or -1 (down).
inline int nudge(int speed, int dir) { return clamp(speed + dir); }

}  // namespace Speed
