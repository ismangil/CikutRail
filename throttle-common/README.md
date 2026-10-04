# throttle-common

Code shared by the CikutRail WiThrottle handhelds
([`throttle-m5stick/`](../throttle-m5stick/) and
[`throttle-touch349/`](../throttle-touch349/)). A PlatformIO library, used with

```
lib_deps = symlink://../throttle-common
```

| File | What it is |
|---|---|
| `src/AppDelegate.h` | `WiThrottleProtocolDelegate` that buffers the roster, turnouts and routes and mirrors the loco state |
| `src/Provision.h` / `.cpp` | SoftAP captive portal for Wi-Fi and server settings, stored in NVS |
| `src/CommonConfig.h` | Wi-Fi, mDNS, throttle slot, reconnect ladder and NVS keys |
| `src/Link.h` / `.cpp` | Wi-Fi join, JMRI discovery (manual host or mDNS), TCP connect and the reconnect ladder, with no UI |
| `src/FunctionSlots.h` | Chooses the three function buttons from a loco's JMRI function labels (plain C++, tested natively in `throttle-touch349`) |

The projects supply `WiThrottleProtocol` themselves. `throttle-m5stick/` does not use `Link` yet; its own copy of that logic is
still in its `main.cpp` and should move over once it can be tested on the
M5Stick.
