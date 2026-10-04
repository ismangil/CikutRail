# CikutRail

Model railway control hardware and software, built around
[JMRI](https://www.jmri.org/) talking MQTT over Wi-Fi.

## Projects

| Folder | What it is | Status |
|---|---|---|
| [`turnout-controller/`](turnout-controller/) | MQTT turnout node on an M5Stack Stamp-S3Bat, driving up to 11 turnouts through IoTT GreenHat coil drivers; emulates JMRI Raspberry Pi GPIO turnouts | Planning |
| [`throttle-m5stick/`](throttle-m5stick/) | Handheld Wi-Fi throttle on an M5StickC Plus 2 with a MiniEncoderC HAT; drives locos and throws turnouts through JMRI's WiThrottle server | Imported, not yet run on the layout |

Each project is self-contained: it builds, tests and documents itself from
its own folder.

## Shared docs

- [`docs/MQTT_CONVENTIONS.md`](docs/MQTT_CONVENTIONS.md): the layout-wide
  MQTT rules (JMRI channel, topics, turnout number ranges, node status
  topics). Every project follows them.
- [`docs/JMRI_WITHROTTLE.md`](docs/JMRI_WITHROTTLE.md): the JMRI WiThrottle
  server the throttles connect to.

## License

[AGPL-3.0](LICENSE)
