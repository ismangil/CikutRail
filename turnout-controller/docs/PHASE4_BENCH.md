# Phase 4 bench test

Phase 4 adds the **config page** on the home network, at
`http://<node>.local` (mDNS) or the node's IP address, behind a login:
user `admin`, password random (made on first use, kept in flash, shown by
`net` on the USB console) and changeable on the page. It has:

- **Turnouts**: the JMRI name of each channel (blank = not subscribed),
  its state, and Close / Throw test buttons. The buttons change the pin
  here only, like the console; JMRI isn't told, and its next command wins.
  New names are saved in flash and MQTT resubscribes.
- **Behaviour**: startup, JMRI offline, stagger, minimum interval (the
  same settings as the console's `config`).
- **Network**: Wi-Fi, MQTT, JMRI channel, node name. MQTT, channel or name
  changes reconnect MQTT only; a Wi-Fi change leaves the network and joins
  the new one.
- **Admin password**.
- **Factory reset**: erases network, names, behaviour, saved levels and
  the admin password (the setup access point's password stays), and opens
  the setup access point. Pins keep their levels.

One web server serves both pages: requests on the setup access point get
the setup page, requests from the home network the config page. Each form
carries a per-boot token, so another website can't submit it with the
browser's saved login.

**Not in phase 4, by decision:** no OTA updates and no Reboot button.
Firmware still goes over USB (which resets the board), and nothing on the
page restarts the node.

## Setup

As in phase 3: node on Wi-Fi, JMRI with MT101, the Kato on channel 1.
Keep the node's console open. Use a phone or laptop on the home network.

## Tests

1. **Reach the page.** `net` on the console shows the page's address and
   the admin password. Open `http://turnout1.local` (or the IP): the
   browser asks for a login; `admin` and the password open the page. A
   wrong password is refused. Some devices (many Android browsers, some
   Windows setups) can't resolve `.local` names; use the IP address, and
   give the node a DHCP reservation so it stays the same.
2. **Setup page not on the home network.** The page at the node's IP
   never shows the setup form, and `http://<IP>/save` answers 404.
3. **Test buttons.** Throw, then Close on channel 1: one movement each,
   `web ch1 -> THROWN (local)` on the console. Enter in a name field
   saves names and doesn't move anything. Saving names resubscribes, so
   the broker re-sends every retained command: a turnout changed with a
   test button goes back to JMRI's last command.
4. **Names.** Rename channel 2 to `120`, save: the console shows the
   resubscribe, `net` lists `track/turnout/120`, and
   `tools/mqtt_exercise.py set 120 CLOSED` changes channel 2. A duplicate
   name (two channels `101`) is refused. Put `102` back.
5. **Behaviour.** Set stagger 500 on the page; `config` on the console
   shows it. Set it back to 0.
6. **Network, same Wi-Fi.** Change nothing but save: `MQTT settings
   changed, reconnecting`, no Wi-Fi drop, nothing moves.
7. **Admin password.** Change it; the browser asks again and the new one
   works. Two different entries, or 5 characters, are refused.
8. **Stale form.** Load the page, power-cycle the node, then press a
   button on the old page: `The page was out of date`, nothing changes.
9. **Factory reset.** Tick the box and reset. Expect the setup access
   point to open (same password as before), nothing to move, and `config`
   to show the defaults. Set the node up again from the setup page.

## Results

| Test | Result | Notes |
|---|---|---|
| 1 Reach the page | | |
| 2 Setup page not on the home network | | |
| 3 Test buttons | | |
| 4 Names | | |
| 5 Behaviour | | |
| 6 Network, same Wi-Fi | | |
| 7 Admin password | | |
| 8 Stale form | | |
| 9 Factory reset | | |

### Run 1, 2026-09-28

Firmware 0.4.0-phase4, same bench as phase 3, browser on a phone.

| Test | Result | Notes |
|---|---|---|
| 1 Reach the page | Pass | Login works; no login or a wrong password gets 401. `turnout1.local` resolved once, then gave "address not found" on the phone; the IP works. The node answers mDNS correctly (checked from the Pi), so it's the phone's `.local` support. |
| 2 Setup page not on the home network | Pass | `POST /save` from the home network gets 404. |
| 3 Test buttons | Pass | Throw and Close each moved the Kato once; Enter in a name field saved names, nothing moved. |
| 4 Names | Pass | Channel 2 renamed `120` answered `track/turnout/120`; a duplicate name was refused; names restored (resubscribing put channel 2 back to its retained CLOSED). |
| 5 Behaviour | Pass | Stagger 500 set on the page showed in `config`. |
| 6 Network, same Wi-Fi | Skipped | |
| 7 Admin password | Skipped | |
| 8 Stale form | Not run | |
| 9 Factory reset | Not run | |

Findings:

- **The web server handles one connection at a time.** An idle open
  connection (browsers keep spare ones) stalls the page for up to 5 s
  (Arduino `WebServer`'s fixed `HTTP_MAX_DATA_WAIT`); measured 4.1 s
  against 0.15 s. The main loop keeps running meanwhile, so turnout
  commands aren't delayed. Left as is; ESP-IDF's `esp_http_server` would
  remove the stall if it becomes a problem.
- **The lost console lines were at least partly the bench tooling**:
  two programs reading the node's USB console at once split its output
  between them. With one reader at a time the output was complete.
