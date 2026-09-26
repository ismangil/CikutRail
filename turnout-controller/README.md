# Turnout controller

A Wi-Fi MQTT turnout node for JMRI, built on the
[M5Stack Stamp-S3Bat](https://docs.m5stack.com/en/core/Stamp-S3Bat).
It drives up to 11 IoTT turnout boards from GPIO G1–G11 and behaves like
JMRI's Raspberry Pi GPIO turnouts: `CLOSED` drives the pin HIGH, `THROWN`
drives it LOW, as a steady level.

**Status:** planning. No firmware yet.

- [Plan](docs/PLAN.md): design, behaviour, build phases
- [Wiring](docs/WIRING.md): pin map, power, IoTT hookup
- Layout-wide topic rules: [../docs/MQTT_CONVENTIONS.md](../docs/MQTT_CONVENTIONS.md)

## Layout

```
turnout-controller/
├── platformio.ini   (phase 1)
├── src/             firmware
├── test/            unit tests that run on a PC
├── tools/           test helpers (MQTT exerciser)
└── docs/
```

Build, flash and first-start instructions will be added with the firmware.
