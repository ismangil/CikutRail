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

The projects supply `WiThrottleProtocol` themselves. Still to move here from
`throttle-m5stick/src/main.cpp`: the mDNS discovery, the reconnect ladder and
the speed and direction maths, once they are separated from the M5Stick UI.
