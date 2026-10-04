# Throttle: M5StickC Plus 2

The layout's handheld Wi-Fi throttle. It talks to JMRI's **WiThrottle**
server over TCP (not MQTT; see
[../docs/JMRI_WITHROTTLE.md](../docs/JMRI_WITHROTTLE.md)). It runs on an
[M5StickC Plus 2](https://docs.m5stack.com/en/core/M5StickC%20PLUS2)
with the [M5Stack MiniEncoderC HAT (SKU U157)](https://docs.m5stack.com/en/hat/MiniEncoderC%20Hat)
plugged into the top 8-pin connector. Connects to JMRI (or any other
WiThrottle server) over WiFi, lets you pick a loco from the JMRI roster on
the device, and drives it with a centre-zero rotary throttle.

## Hardware

| Part                                | Notes                                |
|-------------------------------------|--------------------------------------|
| M5StickC Plus 2                     | ESP32-PICO-V3-02, 135x240 TFT in     |
|                                     | portrait, 200 mAh battery            |
| M5Stack MiniEncoderC HAT (U157)     | I2C @ 0x42, rotary encoder + push    |
|                                     | button + single RGB LED              |

Press the HAT onto the 8-pin connector — no soldering. The HAT speaks I²C on
`SDA = GPIO 0` and `SCL = GPIO 26`, the bus M5Unified leaves alone.

## Getting started

The whole flow, from a fresh M5StickC Plus 2 to driving a loco, takes about
five minutes.

### 1. Build and flash

Needs [PlatformIO](https://platformio.org/) (`pip install platformio`).
WiThrottleProtocol (v1.1.27) and M5Unified (0.2.25) are pinned in
`platformio.ini` and fetched on the first build.

```
pio run -e m5stick-c-plus2 -t upload     build and flash over USB-C
pio device monitor -e m5stick-c-plus2    serial console
```

The `m5stick-c-plus2` board is defined in `boards/` because the pinned
PlatformIO platform has no Plus 2 board of its own. It is the M5StickC
definition with the Plus 2's 8 MB flash; M5Unified detects the real
hardware at run time.

### 2. First power-on

USB-C, hold the power button (the red one) for ~2 s to power on, then
upload. Reset (six-second hold) if the bootloader doesn't catch the first
time. You should see a "WiThrottle / M5StickC Plus 2" splash.

### 3. Connect to your WiFi (first boot)

The device starts an open WiFi access point named `WiThrottle-XXXX` (last
four hex digits of the MAC) and shows the SSID + portal URL on the TFT.

1. On a phone or laptop, join that SSID. Most OSes pop a captive-portal
   sheet; if not, browse to `http://192.168.4.1/`.
2. Pick your home WiFi network from the scan list, enter the password.
3. **Server host / port** — leave blank to auto-discover JMRI via mDNS, or
   enter `192.168.x.y` and `12090` to skip discovery.
4. Press **Save & connect**. The device reboots and connects.

If you want to redo this later, **hold BtnB** (the side button) while
powering on — that clears the stored credentials.

### 4. Start JMRI's WiThrottle server

See [../docs/JMRI_WITHROTTLE.md](../docs/JMRI_WITHROTTLE.md). JMRI
advertises itself via mDNS, so the throttle finds it automatically on the
same subnet. Make sure your JMRI host is on the same WiFi network and on
**channel 10 or below** (the ESP32 can't see higher 2.4 GHz channels).

### 5. Pick a loco and drive

You'll land in the roster picker. Rotate the encoder to highlight a loco
and **push** the encoder to acquire it. After a brief "Acquiring…" splash
the drive view appears, with a centre-zero vertical slider on the right.

Rotate clockwise to add forward speed, counter-clockwise for reverse — the
encoder LED turns green or red to match. Crossing zero automatically issues
a stop-then-flip-direction. Push the encoder to e-stop.

Press **BtnB** to cycle through additional screens: Functions (the functions the roster entry defines, with JMRI labels),
**Layout** (turnouts and routes from JMRI), Status, and **Locos** (the roster,
to switch loco). **BtnA** returns to
Drive from any of them. On the Layout screen, the encoder selects an item
within the active tab, encoder short-press activates it (toggles a turnout
or fires a route), and encoder long-press flips between the Turnouts and
Routes tabs.

## Controls

| Input                          | Action                                              |
|--------------------------------|-----------------------------------------------------|
| Encoder rotation               | Signed throttle in `[-126 .. +126]`. CW from zero   |
|                                | adds forward speed; CCW from zero adds reverse.     |
| Encoder push (short)           | E-stop — throttle snaps to centre and sends         |
|                                | `emergencyStop` to the server.                      |
| Encoder push (long, 1 s)       | Soft centre — sets speed to 0 without e-stop.       |
| Encoder LED                    | Green = forward, red = reverse, off = stopped.      |
| BtnA (front, short)            | Toggle F0 (lights).                                 |
| BtnA (long)                    | Flip polarity (swap CW/CCW meaning, for locos       |
|                                | facing the other way). Persisted to NVS.            |
| BtnB (side, short)             | Cycle: Drive → Functions → Layout → Status → Locos. |
| BtnB (long, on Drive)          | Jump straight to the Locos screen.                  |
| BtnB (held at boot)            | Clear NVS and re-enter setup.                       |

Layout screen controls (turnouts and routes):

| Input                          | Action                                              |
|--------------------------------|-----------------------------------------------------|
| Encoder rotate                 | Move highlight within the active tab.               |
| Encoder push (short)           | Turnout: toggle Close ↔ Throw via `setTurnout`.     |
|                                | Route: activate via `setRoute`.                     |
| Encoder push (long, 1 s)       | Flip tab: Turnouts ↔ Routes.                        |
| BtnA                           | Back to Drive (loco stays acquired).                |

Zero-crossings work like this: as you turn through zero the sketch snaps
the throttle to exactly 0 for one detent and sends `setSpeed(0)`. The next
detent on the other side issues `setDirection(...)` and starts ramping on
the new side. Both the local state and the server stay aligned even if
another client (e.g. WiThrottle on iOS) changes the loco — the
`receivedSpeedMultiThrottle` / `receivedDirectionMultiThrottle` callbacks
fold the server's view back into the local slider.

## Troubleshooting

- **Splash says "MiniEncoderC not detected on I2C 0x42"** — the HAT isn't
  seated, or you have a different M5 encoder. Pop it on firmly. If you
  swap to the original ENCODER HAT (A031) you'll need to change
  `ENCODER_HAT_ADDR` and register offsets in `config.h`.
- **WiFi setup loops back to the captive portal** — verify your AP is on
  2.4 GHz and channel 10 or below. The ESP32 silently refuses higher
  channels.
- **Roster never loads** — the throttle connected to *something* on port
  12090 that wasn't JMRI WiThrottle. Hold BtnB at boot and enter a manual
  host explicitly.
- **Throttle disconnects after a minute of idle** — should not happen
  (WiThrottleProtocol sends heartbeats automatically). If it does, check
  JMRI's WiThrottle preferences for a heartbeat interval > 0.
- **LED is too bright** — adjust the `0x002000` / `0x200000` levels in
  `updateThrottleLed()` in the `.ino`.

Switching loco: on the Locos screen the loco you are driving is highlighted.
Push the encoder on another loco to release the current one and acquire the
new one. The released loco is not stopped; it keeps running at its last
speed under JMRI, so set it to zero first if you want it stopped. BtnA or
BtnB leaves the Locos screen without changing loco.

## Constraints worth knowing

- ESP32 only supports 2.4 GHz WiFi and struggles above channel 10. The
  provisioning portal flags any AP above ch 10.
- The built-in 200 mAh battery gives roughly three to four hours of
  continuous use. The TFT auto-dims after 30 s of input inactivity.
- Only the first WiThrottle slot (`'0'`) is used. Multi-loco consists are
  still supported because the WiThrottle protocol lets you add multiple
  locos to the same slot.
- Roster, turnouts and routes loaded from JMRI are kept in RAM. The MAX
  loco roster size is bounded only by free heap (~200 kB on this part).

## Files

```
throttle-m5stick/
├── platformio.ini
├── boards/m5stick-c-plus2.json  board definition
└── src/
    ├── main.cpp                 state machine, setup() and loop()
    ├── AppDelegate.h            WiThrottleProtocolDelegate subclass
    ├── UI.h / UI.cpp            M5GFX rendering helpers
    ├── EncoderHat.h / .cpp      I2C driver for the MiniEncoderC HAT
    ├── Provision.h / .cpp       SoftAP + captive portal + NVS
    └── config.h                 pin/I2C constants, NVS keys, tunables
```

## History and licence

Imported with its history from
[flash62au/WiThrottleProtocol#1](https://github.com/flash62au/WiThrottleProtocol/pull/1).
The code here is AGPL-3.0 like the rest of CikutRail. The
WiThrottleProtocol library (CC BY-SA 4.0) is an external dependency
fetched by PlatformIO, not copied in.

## Not yet done

- Layout test: drive the GP40, throw `MT101`-`MT111` from the Layout
  screen, check F10/F11 against the uncoupling script (#4) and how the
  function list handles momentary functions.
- Unit tests for the speed/direction maths, encoder acceleration and
  reconnect backoff, moved into `lib/` like the turnout controller.
- Shared code with the second throttle (#6) moves to `throttle-common/`
  when that starts.
- M5StickS3 support (reverted in the imported history).
