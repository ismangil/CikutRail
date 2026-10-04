// Picks which of a loco's JMRI functions go on the throttle's three buttons.
//
// Plain C++ with no Arduino types, so it runs in the native unit tests.
//
// Slot 0 prefers "light", slot 1 "beacon", slot 2 "uncouple". Those keywords
// are matched case-insensitively against the roster labels, so the uncouple
// button is always in the same place under the thumb. Slots left empty are
// then filled with the lowest-numbered defined functions not yet used.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace FunctionSlots {

constexpr int SLOTS = 3;
constexpr int NONE  = -1;

inline bool containsNoCase(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return false;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n) {
            char a = *h, b = *n;
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) break;
            h++; n++;
        }
        if (!*n) return true;
    }
    return false;
}

inline const char *keyword(int slot) {
    static const char *const k[SLOTS] = {"light", "beacon", "uncouple"};
    return (slot >= 0 && slot < SLOTS) ? k[slot] : "";
}

// Labels that act only while held. WiThrottle does not say which functions
// are momentary, so this is a guess from the label.
inline bool looksMomentary(const char *label) {
    return containsNoCase(label, "uncouple") || containsNoCase(label, "delayed");
}

// labels[i] is the label of function Fi; an empty or null string means the
// roster entry does not define it. out[s] gets the function number or NONE.
inline void pick(const char *const *labels, int n, int out[SLOTS]) {
    bool used[64] = {false};
    if (n > 64) n = 64;
    for (int s = 0; s < SLOTS; s++) {
        out[s] = NONE;
        for (int f = 0; f < n; f++) {
            if (used[f] || !labels[f] || !labels[f][0]) continue;
            if (containsNoCase(labels[f], keyword(s))) {
                out[s] = f;
                used[f] = true;
                break;
            }
        }
    }
    for (int s = 0; s < SLOTS; s++) {
        if (out[s] != NONE) continue;
        for (int f = 0; f < n; f++) {
            if (used[f] || !labels[f] || !labels[f][0]) continue;
            out[s] = f;
            used[f] = true;
            break;
        }
    }
}

}  // namespace FunctionSlots
