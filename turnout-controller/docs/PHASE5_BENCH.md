# Phase 5 bench test

Phase 5 adds the node's health signals, all through the PM1:

- **Status LED** (the S3Bat's RGB LED, dim), first match wins:

  | LED | Meaning |
  |---|---|
  | blue blink | setup access point open |
  | red blink | network off: settings unusable |
  | yellow | connecting to Wi-Fi or MQTT |
  | slow green blink | JMRI offline (its OFFLINE seen, not back yet) |
  | green | MQTT up, JMRI online |

  `status` shows the current state; changes are logged.
- **Button**: presses are logged with how long they were held. A **long
  press (3 s)** opens the setup access point, as `portal on` does. The
  PM1's **single-click reset and double-click power-off are disabled** at
  startup, so the button can't cut the node's power (a power cut fires a
  THROWN pulse on CLOSED turnouts). On 0.5.0, with only double-click off
  disabled, a single click power-cycled the node; 0.5.1 disables both.
  Holding the button made the PM1 put the ESP32 into download mode
  (firmware stopped, pins undriven) before the 3 s long press fired;
  0.5.2 also sets the PM1's download-mode lock at every boot (it clears
  only on a power cut). USB flashing doesn't use the PM1, so it still
  works.
- **PM1 watchdog**: **off by default**. The PM1 resets the node if it
  isn't fed in time, and a reset can make turnouts pulse, so it stays off
  until the bench shows what its reset does. At startup the firmware turns
  off any watchdog left running (the PM1 keeps running through an ESP32
  reset). Console: `wdt` shows it, `wdt <5-255>` turns it on (fed every
  second) until power-off, `wdt off`, and `wdt hang` stops feeding it.

## Setup

As in phase 4. Keep the node's console open (one reader only).

## Tests

1. **LED at boot.** After flashing: yellow while connecting, then green.
   `status` shows `LED: green (MQTT up, JMRI online)`.
2. **LED states.** `sudo systemctl stop mosquitto`: yellow; start it:
   green. `portal on`: blue blink; `portal off`: green. Kill PanelPro
   (`pkill -9 -f apps.PanelPro`, no trains moving): slow green blink;
   restart it: green.
3. **Short press.** Press the button briefly: `button pressed`, `button
   released after ... ms`, nothing else (a single click doesn't reset this
   board; `boot` shows the same boot).
4. **Long press.** Hold the button for 3-4 s: `button long press: opening
   the setup access point`, the LED blinks blue. Note anything else the
   PM1 does while it is held (a reset would show in `boot`). `portal off`.
5. **Double-click.** Double-click the button: the node stays on.
   `pm1 btn` shows `single-click reset: disabled`, `double-click power
   off: disabled` and `download mode on a held button: locked`.
6. **Watchdog off.** `wdt`: `off`, PM1 count 0.
7. **Watchdog fed.** `wdt 10`, wait 30 s: no reset (`boot` unchanged),
   `wdt` shows the count staying near 10. `wdt off`.
8. **Watchdog reset (resets the board).** Decide the turnout states
   first: a THROWN turnout is the one expected to pulse if the pins float
   with 5VOUT on. `wdt 10`, then `wdt hang`. Within about 10 s the PM1
   resets the node. Note any movement, and from the boot report: the
   reset reason, `5VOUT at boot` (off would mean the PM1 cut the power)
   and the pin levels. This decides whether the watchdog can be left on.

## Results

| Test | Result | Notes |
|---|---|---|
| 1 LED at boot | | |
| 2 LED states | | |
| 3 Short press | | |
| 4 Long press | | |
| 5 Double-click | | |
| 6 Watchdog off | | |
| 7 Watchdog fed | | |
| 8 Watchdog reset | | |
