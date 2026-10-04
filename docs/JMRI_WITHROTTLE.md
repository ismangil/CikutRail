# JMRI WiThrottle server

Handheld throttles (`throttle-m5stick/`, and the second throttle in #6)
connect to JMRI's WiThrottle server over TCP, not MQTT. They drive locos
and throw turnouts and routes through JMRI, so the Pi-SPROG and the MQTT
turnout nodes need no changes.

## Setup in JMRI

- Start the server: **Tools → Throttles → Start WiThrottle Server**.
- Port: 12090 (JMRI's default). Check or change it under **Edit →
  Preferences → WiThrottle**.
- To start it with JMRI, tick the start-at-launch option in the same
  preferences page.
- mDNS: JMRI advertises the server as `_withrottle._tcp`, which is how the
  throttle finds it. The throttle and the Pi must be on the same subnet.
  Entering a host and port in the throttle's setup page skips discovery.
- Wi-Fi: the ESP32 sees only 2.4 GHz channels 1-10.

## What the throttle uses

- Roster entries (locos), turnouts (`MT101`-`MT111` from the turnout node)
  and routes from JMRI's tables.
- Function buttons F0-F12; whether a function is momentary or latching is
  decided by the roster entry and the WiThrottle server. To be checked for
  the uncoupling functions (#4).
