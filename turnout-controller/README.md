# Turnout controller

A Wi-Fi MQTT turnout node for JMRI, built on the
[M5Stack Stamp-S3Bat](https://docs.m5stack.com/en/core/Stamp-S3Bat).
It drives up to 11 turnouts through IoTT GreenHat Coil Driver boards from
GPIO G1–G11, and behaves like JMRI's Raspberry Pi GPIO turnouts: `CLOSED`
drives the pin HIGH, `THROWN` drives it LOW, as a steady level.

**Status:** phase 6. The node takes JMRI MQTT turnout commands over Wi-Fi,
is set up from its own access point, has a config page on the home
network, shows its state on the RGB LED, and can publish turnout
feedback and run channels as JMRI sensors. Firmware updates stay on USB.

- [Plan](docs/PLAN.md): design, behaviour, build phases
- [Wiring](docs/WIRING.md): pin map, GreenHat hookup and behaviour, power
- [Phase 1 bench test](docs/PHASE1_BENCH.md): pins, pulses, 5VOUT, and
  the run 1 results
- [Phase 2 bench test](docs/PHASE2_BENCH.md): broker, settings, JMRI
  connection, MQTT tests
- [Phase 3 bench test](docs/PHASE3_BENCH.md): startup policy, stagger,
  minimum interval, JMRI offline
- [Phase 4 bench test](docs/PHASE4_BENCH.md): config page
- [Phase 5 bench test](docs/PHASE5_BENCH.md): status LED, button, watchdog
- [Phase 6 bench test](docs/PHASE6_BENCH.md): feedback and sensor mode
- [Sensor experiment](docs/SENSOR_BENCH.md): loco stopping point with a magnet and reed/Hall sensor
- Layout-wide topic rules: [../docs/MQTT_CONVENTIONS.md](../docs/MQTT_CONVENTIONS.md)

## Build

Needs [PlatformIO](https://platformio.org/) (`pip install platformio`).

```
pio test -e native                   unit tests on the PC
pio run -e stamp-s3bat -t upload     build and flash over USB-C
pio device monitor -e stamp-s3bat    serial console (type help)
```

GitHub Actions runs the unit tests and builds the firmware on every push
that touches this folder; the built `.bin` files are attached to the run.

## Layout

```
turnout-controller/
├── platformio.ini
├── lib/turnout_core/   hardware-free logic (channels, commands, level
│                       snapshot); built for the board and for PC tests
├── src/                firmware: pin driver, PM1 power chip, Wi-Fi/MQTT,
│                       console
├── test/               unit tests for lib/turnout_core
├── tools/              mqtt_exercise.py: publishes like JMRI, for tests
└── docs/
```
