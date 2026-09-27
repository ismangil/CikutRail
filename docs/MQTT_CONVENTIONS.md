# MQTT conventions

Layout-wide rules that every CikutRail device follows, so JMRI and all nodes
agree on topics and names. Project-specific details live in each project's
own docs.

## JMRI connection

- JMRI connects to the broker with its **MQTT** connection type.
- JMRI publishes **retained** messages at **QoS 2** by default. Nodes rely on
  the retained messages to recover their state after a reboot or reconnect.
- **Channel** (JMRI's base topic, written `<channel>` below) is empty by
  default in current JMRI. Older JMRI versions defaulted to `/trains/`. All
  nodes must be configured with the same channel as JMRI.

## Turnouts

| Item | Value |
|---|---|
| JMRI system name | `MT<name>`, e.g. `MT101` |
| Command topic (JMRI → node) | `<channel>track/turnout/<name>` |
| Payload | `CLOSED` or `THROWN` (`UNKNOWN` / `INCONSISTENT` are ignored) |
| Feedback topic (node → JMRI, optional) | `<channel>track/turnout/<name>/state` |

- JMRI's default *send* and *receive* topics are the same
  (`track/turnout/{0}`). A node must **never publish on the command topic**:
  JMRI would treat it as a new command. Feedback, when enabled, goes on the
  separate `/state` topic, and the JMRI turnout's receive topic is changed to
  `track/turnout/{0}/state` with feedback mode MONITORING.
- JMRI applies the turnout's *Inverted* setting before it publishes, so nodes
  never invert payloads themselves.

### Turnout number ranges

`<name>` is numeric. Each node owns a block of 100 so names never collide:

| Range | Owner |
|---|---|
| 1–99 | reserved (e.g. mirroring existing Raspberry Pi `PT<n>` names) |
| 101–199 | turnout node 1 |
| 201–299 | turnout node 2 |
| … | one block per further node |

Record each node's block here when it is commissioned.

## Node status

Every node publishes under its own node name, outside JMRI's topic tree:

| Topic | Retained | Payload |
|---|---|---|
| `cikutrail/<node>/status` | yes | `online`, or `offline` (set as the MQTT last-will message) |
| `cikutrail/<node>/info` | yes | JSON: firmware version, IP, Wi-Fi RSSI, uptime, plus device extras (turnout node: 5 V input; battery voltage and charging state where a device has a battery) |

## Future devices

Throttles and other devices use JMRI's own MQTT topics where they exist
(for example `cab/{n}/throttle`, `cab/{n}/direction`, `cab/{n}/function/{f}`)
and add their rows to this document.
