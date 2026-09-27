# CikutRail

Model railway control hardware and software, built around
[JMRI](https://www.jmri.org/) talking MQTT over Wi-Fi.

## Projects

| Folder | What it is | Status |
|---|---|---|
| [`turnout-controller/`](turnout-controller/) | MQTT turnout node on an M5Stack Stamp-S3Bat, driving up to 11 turnouts through IoTT GreenHat coil drivers; emulates JMRI Raspberry Pi GPIO turnouts | Planning |

Each project is self-contained: it builds, tests and documents itself from
its own folder.

## Shared docs

- [`docs/MQTT_CONVENTIONS.md`](docs/MQTT_CONVENTIONS.md): the layout-wide
  MQTT rules (JMRI channel, topics, turnout number ranges, node status
  topics). Every project follows them.

## License

[AGPL-3.0](LICENSE)
