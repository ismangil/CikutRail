# Phase 3 bench test

Phase 3 covers what the node does around startup and when JMRI goes away:

- **Earlier pin drive.** The pins are first driven from a C++ constructor,
  before Arduino's start-up code, to the levels in RTC memory after a
  reset or all LOW after a power cut (5VOUT is off then, so nothing can
  pulse).
- **Startup policy** (`config startup`): `restore` (default) sets the last
  levels before 5VOUT comes on (RTC memory after a reset, flash after a
  power cut); `low` sets all LOW, as JMRI's Pi GPIO turnouts start.
  Levels go to flash 2 s after the last change.
- **Stagger** (`config stagger <ms>`, default 0) between any two changes,
  and **minimum interval** (`config interval <ms>`, default 0) between two
  changes of one turnout. Only the newest command per turnout is kept.
  They apply to MQTT commands; the console's `close`/`throw` act at once
  but count towards them.
- **JMRI offline** (`config offline`): JMRI's last will is
  `<channel>track/state` = `OFFLINE`. `hold` (default) keeps every pin;
  `low` sets all LOW, as the Pi does when JMRI shuts down. A retained
  `OFFLINE` is ignored, since it may be older than the current
  connection.

Settings are saved in flash; `config` shows them.

Restarts are treated as unsafe (the phase 1 reset tests weren't run), so
the firmware never restarts itself. The console `reset` commands stay for
bench use only.

## Setup

As in phase 2: one GreenHat, the Kato on channel 1, node on Wi-Fi, JMRI
PanelPro with the MQTT connection and MT101. Keep the node's console and
`tools/mqtt_exercise.py watch` open.

## Tests

1. **Boot report.** After flashing, `boot` shows `pins first driven ... us
   after app start` (expect a few ms, against about 210 ms in phase 2) and
   the startup line (`restore: levels from RTC memory`, since a flash is
   a reset).
2. **Levels saved.** `config` shows `levels saved in flash`. Click MT101
   Closed in JMRI, wait 3 s, `config` again: channel 1 now H.
3. **Restore from flash.** With MT101 CLOSED, stop the broker
   (`sudo systemctl stop mosquitto`) so MQTT can't restore anything, then
   unplug USB-C for a few seconds and reconnect. The unplug fires a THROWN
   pulse (phase 2 finding). At power-up expect the boot report's
   `restore: levels saved in flash (a power cut), ... changed channels 1`
   and one CLOSED movement as 5VOUT comes on, before Wi-Fi. Start the
   broker again (`sudo systemctl start mosquitto`): the retained CLOSED
   comes back `unchanged`.
4. **Startup low.** `config startup low`, MT101 CLOSED, stop the broker,
   power-cycle. Expect `low: all LOW (THROWN)` and no movement at
   power-up (the unplug's THROWN pulse already left it THROWN). Start the
   broker: the retained CLOSED moves it back. `config startup restore`
   afterwards.
5. **Stagger.** `config stagger 1000`, then `$T set 101-103 CLOSED` and
   `$T set 101-103 THROWN`. Expect the `ch1/ch2/ch3 ->` lines 1 s apart
   (timestamps), and `waiting` lines in between. `config stagger 0`.
6. **Minimum interval.** `config interval 3000`, then click MT101 Closed,
   Thrown, Closed, Thrown in JMRI within about a second. Expect the first
   change at once, one `waiting` for the rest, then after 3 s a single
   change to the last state (THROWN). `config interval 0`.
7. **JMRI offline, hold.** With MT101 CLOSED, kill JMRI so it can't say
   goodbye (`pkill -9 -f apps.PanelPro`; this also drops its Pi-SPROG
   connection, so run it with no trains moving). Within about 90 s (1.5 ×
   JMRI's 60 s MQTT keepalive) expect `JMRI OFFLINE: holding every
   turnout`; nothing moves.
   Restart PanelPro and note what the node logs (`JMRI state "..."`, if
   JMRI publishes anything on connect).
8. **JMRI offline, low.** `config offline low`, MT101 CLOSED, kill JMRI
   again. Expect `JMRI OFFLINE: all turnouts LOW`, then `ch1 -> THROWN`:
   the turnout moves. After restarting JMRI, MT101 shows CLOSED (the
   retained command) while the pin is LOW, until the next click. This is
   the Pi's shutdown behaviour; `hold` avoids the mismatch.
   `config offline hold` afterwards.
9. **Clean JMRI quit.** Quit PanelPro from its menu. Note whether the
   node logs `JMRI OFFLINE` (JMRI publishing it on a clean disconnect) or
   nothing.
10. **Settings kept.** Power-cycle; `config` shows the same settings.

## Results

| Test | Result | Notes |
|---|---|---|
| 1 Boot report | | |
| 2 Levels saved | | |
| 3 Restore from flash | | |
| 4 Startup low | | |
| 5 Stagger | | |
| 6 Minimum interval | | |
| 7 JMRI offline, hold | | |
| 8 JMRI offline, low | | |
| 9 Clean JMRI quit | | |
| 10 Settings kept | | |
