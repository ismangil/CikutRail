# Phase 2 bench test

Phase 2 adds Wi-Fi and MQTT: the node subscribes to JMRI's MQTT turnout
topics and drives the pins as JMRI's Raspberry Pi GPIO turnouts would.
Settings are compiled in for now (phase 4 adds the config page). The USB
console from phase 1 still works alongside.

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
need the same login. Give the Pi a fixed address (a DHCP reservation on
the router): the node connects by IP address, since it doesn't resolve
`.local` names.

### 2. Node settings

```
cd turnout-controller
cp src/local_settings.example.h src/local_settings.h
```

Fill in Wi-Fi, broker address and login, JMRI channel and node name.
`local_settings.h` is git-ignored, so the passwords stay out of the
repository. Without it the firmware builds and runs with Wi-Fi off.

Flash as in phase 1 (name the S3Bat's port by id on a Pi with a
Pi-SPROG), then check the console:

```
net: joining Wi-Fi "..."
net: Wi-Fi up, IP 192.168.0.x, RSSI -55 dBm
net: connecting to MQTT 192.168.0.x:1883
net: MQTT up, subscribed to 11 turnouts
```

`net` shows the same at any time, with each channel's topic and message
counts.

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
another.

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
