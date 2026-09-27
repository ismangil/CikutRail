# Phase 2 bench test

Phase 2 adds Wi-Fi and MQTT: the node subscribes to JMRI's MQTT turnout
topics and drives the pins as JMRI's Raspberry Pi GPIO turnouts would.
The network settings are entered on a setup page served from the node's
own access point and kept in flash; the turnout names are compiled in
until the phase 4 config page. The USB console from phase 1 still works
alongside.

## Setup

Same bench as [phase 1](PHASE1_BENCH.md): one GreenHat, a turnout on
channel 1, S3Bat on USB-C.

### 1. Broker

The node needs a broker it can reach over Wi-Fi. Mosquitto's default
configuration listens on localhost only. To open it to the LAN with a
login (on the Pi that runs JMRI):

```
sudo mosquitto_passwd -c /etc/mosquitto/passwd cikutrail
sudo tee /etc/mosquitto/conf.d/cikutrail.conf <<'EOF'
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
EOF
sudo systemctl restart mosquitto
```

This replaces the localhost-only listener, so JMRI and the test tool
need the same login.

Or, on a private network, without a login (anyone on the LAN can then
move turnouts):

```
printf 'listener 1883\nallow_anonymous true\n' | sudo tee /etc/mosquitto/conf.d/cikutrail.conf
sudo systemctl restart mosquitto
```

This layout's broker runs this way. Give the Pi a fixed address (a DHCP reservation on
the router): the node connects by IP address, since it doesn't resolve
`.local` names.

### 2. Node settings: the setup page

Flash as in phase 1 (name the S3Bat's port by id on a Pi with a
Pi-SPROG). With nothing saved, the node opens a setup access point and
prints how to join it:

```
net: no network settings
portal: open (no network settings saved): join Wi-Fi "CikutRail-440C", password k7mq..., then http://192.168.4.1
```

The password is random, made on first use and kept in flash; `portal`
prints it again. Join that network from a phone or laptop; the setup
page opens by itself (or browse to `http://192.168.4.1`). Fill in:

- **Wi-Fi**: tap your network in the scan list, enter its password.
- **MQTT broker**: the Pi's IP address, port 1883, user and password
  blank for an anonymous broker.
- **JMRI channel**: blank for current JMRI. **Node name**: `turnout1`.

**Save and connect** stores the settings in flash and joins the network
straight away, without restarting, so the turnouts keep their
positions. The page then shows the node's address. The setup network
closes about 30 s after the node joins; reconnect the phone to your
usual Wi-Fi. The console shows:

```
portal: settings saved, joining "..."
net: joining Wi-Fi "..." (settings saved)
net: Wi-Fi up, IP 192.168.0.x, RSSI -55 dBm
net: connecting to MQTT 192.168.0.x:1883
net: MQTT up, subscribed to 11 turnouts
portal: closed
```

The settings survive power cuts and reflashing. `net` shows them (not
the passwords), each channel's topic and message counts. To change them
later: `portal on`, then the same page (it is only served on the setup
network, never on the home network). `net forget` erases them and opens
the page as on first start. If the saved network can't be joined for
3 minutes, the access point opens again by itself, and the node keeps
retrying the saved network every 2 minutes while nobody is on the page.

`src/local_settings.h` (a copy of `local_settings.example.h`,
git-ignored) can prefill the page, or be used directly while nothing is
saved.

### 3. JMRI

- **Edit → Preferences → Connections**, add a connection: System
  manufacturer **MQTT**, connection **MQTT Connection**, host `localhost`,
  port 1883, the broker login. Leave the channel at its default (empty in
  current JMRI) and put the same value in `kJmriChannel`.
- **Tools → Tables → Turnouts → Add**: system connection MQTT, hardware
  address `101` (system name `MT101`). Set the feedback mode to
  **DIRECT**.

### 4. Test tool

`tools/mqtt_exercise.py` publishes exactly as JMRI does (retained, QoS 2),
so the tests don't depend on clicking in a panel. It needs paho-mqtt:

```
sudo apt install python3-paho-mqtt
export MQTT_USER=cikutrail MQTT_PASSWORD=...
T=tools/mqtt_exercise.py
$T node turnout1          # retained status and info
$T watch                  # everything the node and JMRI publish
```

## Tests

Keep `$T watch` running in one terminal and the node's console in
another. Tests 11–14 cover the setup page; run them after 1–10, or
first if the node isn't on the network yet.

1. **Connect.** After boot, the console shows Wi-Fi and MQTT up.
   `$T node turnout1` shows `status` = `online` and an `info` JSON
   (firmware, IP, RSSI, uptime, 5 V input).
2. **JMRI commands.** Click MT101 Closed, then Thrown, in the turnout
   table or a panel. Expect one movement each and
   `mqtt 101 CLOSED: ch1 -> CLOSED` (then THROWN) on the console.
3. **Repeat is a no-op.** `$T set 101 THROWN` twice. Expect `unchanged`
   on the console and no pulse.
4. **Other payloads.** `$T payload 101 UNKNOWN`, then `INCONSISTENT`, then
   `closed` (lower case). Expect `ignored` each time and no movement.
   JMRI reads these too and may show MT101 as unknown; `$T set 101
   THROWN` puts it back.
5. **Repeated changes.** `$T cycle 101 10 2000`. Expect 10 clean
   movements, ending THROWN.
6. **Burst.** `$T set 101-111 CLOSED`, then `$T set 101-111 THROWN`.
   Expect 11 `->` lines each time, and `net` showing 0 dropped.
7. **Retained restore.** `$T set 101 CLOSED`, then unplug USB-C for a few
   seconds and reconnect. Expect a cold start (all LOW, no movement), then
   `mqtt 101 CLOSED (retained): ch1 -> CLOSED` once MQTT is up. The
   turnout is already CLOSED, so it gets a CLOSED pulse but doesn't move.
   Finish with `$T set 101 THROWN`.
8. **Last will.** Unplug USB-C. Within about 25 s (1.5 × the 15 s
   keepalive) `watch` shows `cikutrail/turnout1/status 'offline'`.
   Reconnect: `online` again.
9. **Broker restart.** `sudo systemctl restart mosquitto`. The node logs
   `MQTT down`, then `MQTT up` within about 10 s; the retained commands
   come back as `unchanged` and nothing moves.
10. **Wi-Fi drop (optional).** Restart the access point, or move the node
    out of range and back. The node logs `Wi-Fi down` / `up` and MQTT
    reconnects by itself; nothing moves.
11. **First setup.** After flashing (nothing saved), the access point
    opens and the page is reachable; save the settings. Expect the node
    to join and connect without a reset (the boot report from `boot` is
    unchanged), the page to show its address, and the access point to
    close about 30 s later. Nothing moves.
12. **Bad input.** On the page, save with a 5-character Wi-Fi password,
    or `abc` as the port. Expect `Not saved: ...` naming the field, and
    nothing changed.
13. **Wrong password.** `portal on`, save a wrong Wi-Fi password. Expect
    the status page to stay at "joining". After 3 minutes the node isn't
    on Wi-Fi; `portal on` (if it closed), save the right password: it
    joins. Nothing moves.
14. **Settings kept.** Unplug USB-C and reconnect. Expect `settings
    saved` in `net` and MQTT up without the setup page.

## Results

| Test | Result | Notes |
|---|---|---|
| 1 Connect | | |
| 2 JMRI commands | | |
| 3 Repeat is a no-op | | |
| 4 Other payloads | | |
| 5 Repeated changes | | |
| 6 Burst | | |
| 7 Retained restore | | |
| 8 Last will | | |
| 9 Broker restart | | |
| 10 Wi-Fi drop | | |
| 11 First setup | | |
| 12 Bad input | | |
| 13 Wrong password | | |
| 14 Settings kept | | |

### Run 1, 2026-09-27

Same bench as phase 1 run 1 (one GreenHat, Kato snap coil on channel 1,
S3Bat on USB-C), mosquitto on the Pi listening anonymously on the LAN.
Test 2 used JMRI PanelPro; the other tests used `tools/mqtt_exercise.py`.

| Test | Result | Notes |
|---|---|---|
| 1 Connect | Pass | Wi-Fi at -64 dBm, MQTT up, 11 topics subscribed, retained `online` and `info`. |
| 2 JMRI commands | Pass | MT101 Closed / Thrown in JMRI's turnout table: one movement each. JMRI read the retained state at start (MT101 showed THROWN) and publishes retained at QoS 2 on `track/turnout/101`, so its channel is empty. |
| 3 Repeat is a no-op | Pass | Second THROWN counted `unchanged`, no pulse. |
| 4 Other payloads | Pass | UNKNOWN, INCONSISTENT, `closed`: ignored, no movement. |
| 5 Repeated changes | Pass | 10 clean movements. |
| 6 Burst | Pass | 11 CLOSED then 11 THROWN: 22 applied, 0 dropped. |
| 7 Retained restore | Pass | Retained CLOSED re-applied after a power cycle; the turnout ended CLOSED. But the unplug itself fired a THROWN pulse (see below). |
| 8 Last will | Pass | `offline` within 30 s of the unplug; `online` about 2 s after power-up. Nothing moved with all channels THROWN. |
| 9 Broker restart | Pass | `MQTT down`, then up by itself; 11 retained commands `unchanged`, nothing moved. |
| 10 Wi-Fi drop | Not run | Optional. |
| 11 First setup | Pass | Setup page from the phone; joined without a reset (boot report unchanged); access point closed by itself. |
| 12 Bad input | Not run | |
| 13 Wrong password | Not run | |
| 14 Settings kept | Pass | Power cycles in 7 and 8 came back with `settings: saved`, no setup page. |

Findings:

- **Power loss with a turnout CLOSED fires a THROWN pulse.** Unplugging
  USB-C with ch1 CLOSED (pin HIGH) and the coil supply on moved the
  turnout to THROWN; with ch1 THROWN nothing moved. The retained restore
  then put it back to CLOSED. Recorded in WIRING.md.
- **Pins are driven about 210 ms after app start** (phase 1: about
  103 ms), on every power-on, not only the first boot after flashing.
- **ch2's pad read HIGH at a power-on**, before the firmware drove it,
  with 5VOUT off. Cause unknown.
- **The console sometimes loses output** (one `unchanged` line, one
  `status` reply), and stray input arrives after the port reappears. The
  node's own counters were right each time.
