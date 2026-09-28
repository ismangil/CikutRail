# Dual-GreenHat carrier board: design note

Status: **placement started**. The schematic and a first PCB placement
(70 × 56 mm, not yet routed) are generated in [easyeda/](easyeda/).

One PCB that the Stamp-S3Bat plugs into, carrying two copies of IoTT's
GreenHat Coil Driver circuit: six turnout channels for this layout's six
Kato turnouts. It replaces two GreenHat boards, the servo cables and the
loose 5VOUT/GND wiring described in [WIRING.md](../../docs/WIRING.md),
and the node's separate 5 V supply: one 12 V screw terminal powers both
the coils and the S3Bat.

The firmware doesn't change. The board uses channels 1–6 (G1–G6); channels
7–11 stay available for layouts built from separate GreenHats.

## Decisions so far

| Topic | Decision |
|---|---|
| Channels | 6 (two GreenHat circuits of 3) |
| Turnouts | Kato Unitrack only: one two-wire coil per turnout |
| S3Bat mounting | **Stamp-S3Bat DIP** (S015-DIP, headers pre-soldered) in two 1×9 female 2.54 mm headers, S3Bat removable |
| Power supply | **Fixed 12 V**: Pro-Elec PEL01531 adapter, 12.0 V, 5 A, regulated. No wide input range |
| Power input | One 5.08 mm 2-pin screw terminal, as on the GreenHat (KF128L-5.08-2P, LCSC C474940). It powers the coils *and* the S3Bat |
| Input fuse | **PTC resettable fuse**, SMD1812P200TF16 (LCSC C20812): 2 A hold, 4 A trip, 16 V |
| Reverse polarity | One **SS34** Schottky in series (LCSC C8678, JLC Basic), replacing the GreenHat's Q1/D7/R7 |
| Node power | 12 V → 5 V buck feeding the S3Bat's **5VIN** pin: **TI TPS563201DDCR** (LCSC C116592) |
| Turnout outputs | **Two 6-way 2.54 mm screw terminals**, KF128-2.54-6P (LCSC C474924), one per block: 2 pins per turnout, 3 turnouts each |
| Assembly | Fully assembled by JLCPCB, parts from LCSC |
| CAD | EasyEDA |

## Source design

IoTT GreenHat Coil Driver rev 1.0 (Hans R. Tanner, 2021), in the IoTTStick
repo under `Hat Devices/GreenHat Power Extension/`:

- `Schematics/SCH_M5 GreenHat Coil Driver_2021-08-27.json`: EasyEDA
  schematic, the starting point.
- `Build your own/BOM_...csv`, `PickAndPlace_...csv`, `Gerber_...zip`.
  There is no EasyEDA PCB source, so the layout is drawn fresh.

**License:** the GreenHat is published under the TAPR Open Hardware
License v1.0. This board is a derivative, so its design files go out under
TAPR OHL v1.0 too (not the repo's AGPL), with credit to IoTT. See
[LICENSE.md](LICENSE.md): the license text, the unchanged upstream files
(`upstream/`) and the list of modifications ([CHANGES.txt](CHANGES.txt))
are in this folder, as the OHL requires. Update CHANGES.txt with each
design change, and add Gerbers and schematic PDFs here once they exist.

## The Stamp-S3Bat DIP socket

From M5Stack's [Stamp-S3Bat DIP page](https://docs.m5stack.com/en/core/Stamp-S3Bat_DIP):
the [schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1222/S015-SCH_Stamp-S3BAT.pdf)
(headers J3 and J5), the
[size drawing](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1222/S015-DIP-stamp-s3bat-DIP-model-size.pdf)
and the [KiCad footprint](https://github.com/m5stack/M5_Hardware/blob/master/KiCad/Footprints/M5Stack.pretty/Stamp-S3Bat.kicad_mod).

**Mechanics**

- Module 18.0 × 29.8 mm, 12 mm tall including the pins.
- Two rows of 9 pins at 2.54 mm pitch, rows **15.24 mm apart** (6 × 2.54),
  so the socket sits on the 0.1″ grid: a 600 mil DIP-18 layout. The rows
  are 1.38 mm in from the long edges.
- Don't use M5Stack's KiCad footprint for the socket. It's for the
  castellated (non-DIP) Stamp-S3Bat: its edge pads are 17.3 mm apart, and
  it adds five 1.27 mm-pitch pads (USB D+/D−, BOOT, USBIN, GND) on the
  short edge that the DIP version doesn't bring out. Draw two 1×9 female
  headers 15.24 mm apart instead.
- USB-C is on the short edge at the **pin 9 end (G7 / GND)**, confirmed
  on the module; pin 1 (G1 / 5VIN) is at the far end. That matches the
  footprint, whose extra USB pads sit next to G7. Put the USB-C end at the
  carrier's board edge so a cable still fits once the module is plugged
  in. The PWR button is on the module's top face and stays reachable.
- The module carries a 24-pin BTB connector and an FPC Wi-Fi antenna.
  Keep the area between the header rows free of tall parts, and keep
  copper pour and metal (screw terminals, the VM traces) away from
  under the module, to give the antenna room.

**Pinout** (top view, USB-C end towards you: the G1–G7 row is then on
the left. Pin 1 is the far end of each row, pin 9 the USB-C end.)

| Pin | Left row (J3) | Carrier use | Right row (J5) | Carrier use |
|---|---|---|---|---|
| 1 | G1 | ch1 | 5VIN | **5 V from the carrier's buck**, see "Node power" |
| 2 | G2 | ch2 | 3V3 (3V3_L2) | optional: input pull-ups (see below) |
| 3 | G3 | ch3 | VBAT | not connected |
| 4 | G4 | ch4 | G8 | not connected |
| 5 | G5 | ch5 | G9 | not connected |
| 6 | GND | logic GND | G10 | not connected |
| 7 | **5VOUT** | VCC5.0 (GreenHat logic) | G11 | not connected |
| 8 | G6 | ch6 | WAKE (PY_G4_WAKE) | not connected |
| 9 | G7 | not connected (spare test pad) | GND | logic GND |

So the **left row alone** carries all six channels, 5VOUT and GND. The
right row carries the 5VIN feed, GND and the optional 3V3 pull-up rail;
its other pins just hold the module in place.

**Power pins, from the schematic**

- **5VOUT** (EXT_5V_OUT): an SY7088 boost from the module's system rail,
  enabled by the PM1 (`5VOUT_EN`). It's off at power-on until the firmware
  turns it on, as the bench tests showed.
- **5VIN**: goes into the module's power path through an ideal-diode stage,
  the same as USB-C. So a 5 V supply on the carrier can power the node
  through this pin, and USB-C can stay plugged in for flashing (each
  source has its own ideal diode). It needs a regulated 5 V, never the
  12 V supply directly.
- **3V3** (3V3_L2): the ESP32's own rail (JW5712 buck, PM1-enabled). It's
  up whenever the ESP32 runs, including through an ESP32 reset, so it
  can carry the 10.2 kΩ input pull-ups (6 × ~0.3 mA).
- **VBAT**: the battery rail. This board doesn't use a battery.

## How one GreenHat channel works (from the schematic)

Per channel *n* (1–3):

- `INn` (header pin 3) has a 10.2 kΩ pull-up to VCC5.0 and 100 nF to GND.
- `INn` drives one DRV8313 half-bridge directly.
- `INn` also feeds an RC delay: 10.2 kΩ + 1 MΩ trimmer (3386P) into
  10 µF. That drives one 74HC86 gate, wired as a buffer (other input
  tied to GND), whose output `inpINn` drives a second half-bridge.
- The coil sits between the two half-bridge outputs (`DRVn-0`, `DRVn-1`,
  terminal pins 1–2). Each edge on `INn` gives one pulse, whose length
  the trimmer sets.
- Terminal pins 3–4 (`DRVn-R`, `DRVn-L`) come from those two outputs
  through SM4007PL diodes. They serve twin-coil turnouts with a common
  wire. **Kato doesn't need them**, so this board drops the diodes and
  pins 3–4.

The three channels share two DRV8313s (U2: ch1 direct, ch1 delayed,
ch2 direct; U1: ch2 delayed, ch3 direct, ch3 delayed) and one 74HC86
(gates 1–3; gate 4 unused, inputs tied).

Shared parts per GreenHat:

- **Reverse-polarity protection:** Q1 (SI2301 P-MOSFET), D7 (9.1 V
  zener gate clamp) and R7 (1.5 kΩ) between the screw terminal and VM.
- **5 V regulator:** U6 (H7350-A LDO from VM).
- **J6** (2×2): when bridged, it connects U6's output to VCC5.0 **and**
  logic GND to power GND. With J6 open, as in this layout, the grounds are
  joined only through the DRV8313s and the external wiring.

## What changes on the carrier board

| Block | GreenHat ×2 | This board |
|---|---|---|
| DRV8313 | 4 | 4 (unchanged) |
| 74HC86 | 2 | 2 × **74HCT86D** (LCSC C6005), 6 gates used, 2 spare. See below |
| RC delay + 1 MΩ trimmer | 6 | 6 RC delays, **no trimmers**: fixed 10 kΩ × 10 µF (see "Pulse length") |
| Input pull-up + 100 nF | 6 | 6. Pull-ups stay on 5 V; 10 kΩ (Basic part) instead of 10.2 kΩ |
| RC delay's series resistor | 10.2 kΩ | 10 kΩ (Basic part) |
| DRV8313 charge-pump cap (CP1–CP2) | 100 nF | **10 nF**, as the DRV8313 datasheet specifies |
| DRV8313 V3P3OUT cap | 4.7 nF | **470 nF**, as the DRV8313 datasheet specifies |
| DRV8313 VM decoupling | 100 nF per chip pair | 100 nF per VM pin, plus 22 µF bulk per block |
| Steering diodes D1–D6 | 12 | **none** |
| Output terminals | 6 × 4-pin | **2 × 6-way** (2 pins per turnout) |
| Power terminal + Q1/D7/R7 | 2 sets | **1 terminal + 1 SS34 diode**; Q1/D7/R7 dropped |
| H7350 LDO + J6 | 2 | **none**. VCC5.0 comes from the S3Bat's 5VOUT |
| 12 V → 5 V buck | none | **new**: feeds the S3Bat's 5VIN (see "Node power") |
| Input headers J1–J3 | 6 | **none**. GPIOs routed on the board |
| GND / PwrGND | joined by J6 or wiring | **joined on the board** at one point near the power terminal |
| Test pins J7/J8 | 4 | optional test pads |

### Logic rail: S3Bat 5VOUT

VCC5.0 (the 74HC86s, the input pull-ups and the DRV8313 logic pins) comes
from the S3Bat's **5VOUT** pad, just as on the bench (J6 open). That keeps
the firmware's power sequencing as it is: pins driven first, then 5VOUT on
(PLAN.md, startup). The draw is about 10 mA.

### Input levels: an opportunity to fix the 3.3 V margin

PLAN.md notes that 3.3 V drive into a 74HC86 at 5 V is out of spec: the
HC gate's V_IH is 3.5 V. It works on the bench, but it isn't guaranteed.
A new board can fix this cheaply:

1. **74HCT86** in place of 74HC86 (same SOIC-14 pinout, V_IH 2.0 V at 5 V).
   The DRV8313 inputs already accept 3.3 V (V_IH 2.2 V).
2. Optionally, move the 10.2 kΩ input pull-ups from 5 V to the S3Bat's
   **3V3**. The inputs are still HIGH (CLOSED) when the pins aren't
   driven, so the behaviour in WIRING.md, "Consequences", doesn't change.
   The lines also no longer sit at 5 V against a 3.3 V ESP32 pin during a
   reset. The S3Bat's 3V3 is on its right-row pin 2 and is up whenever
   the ESP32 runs, so it's up whenever 5VOUT is.

**On the schematic:** option 1 (74HCT86D). The pull-ups stay on 5 V, as
on the GreenHat. A quick bench check on a GreenHat with its U3 swapped for
an HCT86 would still be worthwhile before ordering.

### Power supply: fixed 12 V

The layout supply is a **Pro-Elec PEL01531** mains adapter (model
CGSW65C-120-5000II): **12.0 V DC regulated, 5.0 A, 60 W**. The board is
designed for that fixed 12 V (roughly 11–13 V), not a wide input range,
which keeps the power section small:

- one Schottky diode for reverse polarity instead of a MOSFET, zener and
  resistor;
- a cheap buck converter rated to 17 V instead of one rated to 28 V;
- 25 V-rated capacitors throughout the 12 V side.

**Don't use a supply above 13 V with this board.** The GreenHat's usual
12–16 V range no longer applies.

The adapter's 5 A is far more than the board draws: about 0.2 A for the
node at 12 V, plus one coil pulse at a time.

### Reverse polarity: one Schottky diode

A screw terminal can be wired backwards, so the input keeps a
reverse-polarity guard, but a simpler one:

- **SS34** Schottky in series with the 12 V input, LCSC **C8678**
  (MDD, JLC **Basic**, SMA): 40 V, 3 A, 0.55 V at 3 A. About 4.5M were in
  stock on 2026-09-28, at $0.03 each.
- It replaces the GreenHat's Q1 / D7 / R7: three parts, two of them
  Extended, become one Basic part.
- **Cost:** a 0.4–0.55 V drop. The coils see about 11.5 V, and the
  buck is unaffected. Dissipation is about 0.1 W idle (node only) and
  about 0.5 W during a 1 A coil pulse lasting milliseconds.
- **Current:** 3 A continuous covers one coil at a time (each DRV8313
  output is limited to about 2.5 A peak). If the Kato coil figures come
  in higher, the SS54 (C22452, Basic, 5 A, same SMA footprint) is a
  drop-in.
- Reversed, the diode blocks and nothing on the board sees −12 V.

The AO3401A / AO3407A MOSFETs picked earlier aren't needed any more.

### Node power: 12 V → 5 V buck into 5VIN

After the diode, the 12 V rail (VM) feeds the DRV8313s and a small buck
converter. The buck's 5 V output goes to the S3Bat's **5VIN** (right-row
pin 1).

- **Load:** the S3Bat on Wi-Fi draws a few hundred mA, with short peaks.
  The GreenHat logic hangs off the module's own 5VOUT boost (~10 mA),
  which in turn runs from 5VIN. Size the buck for **1 A** or more.
- **Chosen: TI TPS563201DDCR**, LCSC **C116592** (JLC Extended): 4.5–17 V
  input, 3 A, 580 kHz, SOT-23-6. Its internal compensation means no
  compensation parts. About 160k were in stock on 2026-09-28, at $0.07
  each.
  - With a fixed 12 V input (about 11.5 V after the diode), 17 V is
    ample headroom.
  - Its datasheet has a table of inductor, output capacitor and
    feedback resistor values for a 5 V output; use it as is.
  - Follow the datasheet's reference layout: short switching loop,
    inductor close, input caps at the pins.
- **Earlier choice, dropped:** the TPS54202DDCR (C191884, 28 V). It's only
  worth it for a wide input range.
- **Keep it away from the antenna:** put the buck and its inductor at the
  power-terminal end of the board, not under or beside the S3Bat.
- **Input filtering:** bulk capacitance at the buck input, so a coil pulse
  on the shared 12 V doesn't brown out the node. A small series
  resistor or ferrite between VM and the buck input, with its own
  capacitor, helps further.
- **Solder jumper** in the 5 V line to 5VIN, closed by default, so the
  node can be bench-tested from USB-C with the buck disconnected. Add a
  5 V test pad.
- **USB-C still works:** 5VIN and USB-C each go through their own ideal
  diode on the S3Bat, so flashing over USB-C with 12 V present is fine.

What changes in behaviour:

- **The node's power follows the layout's 12 V.** Switching the 12 V off
  shuts the node down; switching it on is a cold start. The firmware
  already handles that: 5VOUT is off at power-on, so the pins are driven
  to the restored levels before the GreenHat logic comes up (PLAN.md,
  startup). No firmware change.
- The WIRING.md note "5VIN carries 5 V only when that supply is
  connected, not when the node runs from USB-C" still holds; on this
  board "that supply" is the buck.

### Coil power

- One 12 V supply for all six channels, through the one SS34. The
  firmware's stagger scheduler fires one turnout at a time, so the peak
  is one coil.
- Kato Unitrack coils: confirm the current Kato specifies at 12 V. The
  DRV8313 is rated to about 2.5 A peak per output (and runs from 8 V, so
  11.5 V is well inside its range). Size the VM traces and the terminal
  for one coil's peak current plus margin.
- Bulk capacitance on VM near each DRV8313, as on the GreenHat (100 nF
  per VM pin), plus one bulk electrolytic or ceramic (25 V) at the input.
- **Input fuse (included):** a 60 W adapter can push 5 A into a wiring
  fault before its own protection trips, so a resettable PTC fuse sits
  between the screw terminal and the SS34.
  - **Part:** SMD1812P200TF16, LCSC **C20812** (RUILON, 1812). It holds
    2 A, trips at 4 A and is rated 16 V. About 30k were in stock on
    2026-09-28, at $0.11 each.
  - **Why this one:** many 2 A 1812 PTCs are rated only 8 V, too low for
    12 V.
  - **Pulses don't trip it:** a millisecond coil pulse of 1–2.5 A is well
    inside the hold rating, so only a sustained fault trips it. It
    resets once the fault is gone and it cools.
  - **Drop:** about 0.1 Ω, under 0.1 V at the node's idle current.

### Pulse length: fixed, no trimmers

The GreenHat's 1 MΩ trimmers are left out.

- **Why it's safe:** on the layout they're set fully counter-clockwise.
  That puts the wiper at pin 1: a meter reads **0 Ω** across the trimmer,
  confirmed 2026-09-28. So the trimmer adds nothing, and the pulse comes
  from the 10 kΩ series resistor and the 10 µF capacitor alone
  (τ ≈ 100 ms).
- **How the length arises:** the delayed side follows the input once the
  capacitor crosses the XOR gate's threshold. The S3Bat drives the line
  between 0 V and 3.3 V.

  | Gate | CLOSED pulse (rising edge) | THROWN pulse (falling edge) |
  |---|---|---|
  | 74HC86 (today, threshold ~2.5 V) | ~140 ms | ~30 ms |
  | 74HCT86 (this board, threshold ~1.4 V) | ~55 ms | ~85 ms |
  | 74HCT86, over its spec threshold range (0.8–2.0 V) | 30–95 ms | 50–140 ms |

- **Kato:** both new lengths lie between the two the layout's turnouts
  already switch with, so they should throw reliably. The HCT86 also
  makes the two directions more even.
- **To change it:** the length scales with the series resistor
  (R104–R106, R204–R206). For example, 4.7 kΩ roughly halves it.
- **Saved:** about 650 mm² of board and six through-hole parts, and
  nothing needs adjusting.

### Outputs and turnout direction

Each turnout uses a pair of terminal pins carrying `DRVn-0` / `DRVn-1`: on J101, pins 1–2 are channel 1, 3–4 channel 2 and 5–6 channel 3; J201 does the same for channels 4–6. A Kato coil moves one
way or the other depending on polarity, so a turnout wired backwards is
fixed by swapping its two wires or setting JMRI's "Inverted". Label the
pins on the silkscreen consistently, e.g. `1A/1B` … `6A/6B`.

## Channel map

| Channel | S3Bat pad | Driver block | Terminal |
|---|---|---|---|
| 1 | G1 | A, ch1 | J101 pins 1–2 |
| 2 | G2 | A, ch2 | J101 pins 3–4 |
| 3 | G3 | A, ch3 | J101 pins 5–6 |
| 4 | G4 | B, ch1 | J201 pins 1–2 |
| 5 | G5 | B, ch2 | J201 pins 3–4 |
| 6 | G6 | B, ch3 | J201 pins 5–6 |

All six are on the S3Bat's **left** row, together with 5VOUT and GND
(see "The Stamp-S3Bat DIP socket").

## Board size (estimate)

No layout yet, so this is an estimate.

- **Calibration:** IoTT's GreenHat board, from its Gerber outline, is
  **52 × 45 mm** (about 2,360 mm²) for three channels.
- **This board has:**
  - two GreenHat circuits, minus the steering diodes, input headers, LDO
    and one of the two power-input circuits: about 4,000 mm²;
  - the S3Bat socket area, 18 × 30 mm with clearance around it: about
    700 mm²;
  - the buck converter, fuse and diode: about 300 mm².
- **Minus the trimmers:** the six 3386P trimmers (9.5 × 9.5 mm each) are
  gone, saving about 650 mm².
- **Total:** about 4,350 mm², so roughly **70 × 60 mm**. The four
  DRV8313s with their capacitors now take the most room.
- **Edges:** the two 6-way output terminals (KF128-2.54-6P, C474924,
  about 1.6k in stock) sit end to end along one edge: 12 positions ×
  2.54 mm ≈ 31 mm. The 5.08 mm power terminal (about 10 mm) goes beside
  them, and the S3Bat on another edge, with USB-C facing out.
  - Six 2-pin blocks or one 12-way block (C474929, only 28 in stock on
    2026-09-28) would be the same length.
  - Two 6-way blocks were chosen for fewer parts with healthy stock.
- **Cost:** it stays well under JLCPCB's 100 × 100 mm price tier. Two
  layers should be enough.

## Open questions

1. **S3Bat socket check.** The pinout and 15.24 mm row spacing come from
   M5Stack's schematic and drawing (see above). Before ordering, print
   the layout 1:1 and set the module on it to confirm pin 1, the
   orientation (USB-C end) and that the pins fit the chosen female
   headers.
2. **Buck converter passives:** chosen from the TPS563201 datasheet
   (table 7-2, 5 V row).
   - Inductor: 3.3 µH Sunlord SWPA4030S3R3MT (C15269, 2.4 A).
   - Output: 2 × 22 µF.
   - Feedback divider: 100 kΩ / 18 kΩ (5.03 V), both Basic parts.
   - Input: 2 × 10 µF + 100 nF.
   - Bootstrap: 100 nF.
   - EN: pulled up to VIN through 100 kΩ (EN is rated to 19 V).
3. **Kato coil figures** (current at 12 V) for trace sizing.
4. **LCSC stock:** check every part at order time.
5. **Board outline and mounting:** size, mounting holes, whether it fits
   an enclosure.
6. **HCT86:** on the schematic; confirm on the bench (see "Input
   levels").
7. **74HCT86D stock:** only about 1k at LCSC (C6005) on 2026-09-28. That's
   fine for a small batch, but check at order time.

## Considered, not chosen

- **Firmware-timed pulses** (drive both half-bridges from GPIOs, no RC,
  XOR or delay parts): needs 12 GPIOs for 6 turnouts and a firmware change,
  and it throws away the GreenHat behaviour the firmware is built around.
  Keeping the GreenHat circuit means the board is a known design.

## Schematic (EasyEDA)

The schematic is generated from a script instead of drawn by hand, so the
circuit lives in one reviewable place:

- [easyeda/build_schematic.py](easyeda/build_schematic.py) defines every
  part (by LCSC number) and every pin's net. It writes:
  - `DualGreenHat_schematic.json`: the EasyEDA Standard source;
  - `netlist.txt`: each net and its pins, for review;
  - `bom.csv`: a JLCPCB-style BOM.
- `easyeda/lib/` caches each part's EasyEDA symbol, fetched from EasyEDA's
  LCSC library, so the build runs offline.
- Every connected pin gets a net label; there are no drawn wires. It's
  plain to read and easy to diff, but less pictorial than IoTT's hand-drawn
  sheets. Tidy it by hand in EasyEDA if you like, but then treat the
  EasyEDA copy as the master and stop regenerating.
- Sheets: **Power and S3Bat** (terminal, fuse, SS34, buck, sockets),
  **Channels 1-3** and **Channels 4-6** (one GreenHat circuit each).
- Designators: block 1 uses the 100s (U101 is the XOR, U102/U103 the
  drivers), block 2 the 200s, and the power sheet 1–9. The turnout
  terminals are J101 (channels 1–3) and J201 (channels 4–6).
- Checked in EasyEDA Standard: loading a sheet built the same way,
  EasyEDA matched every part to its LCSC library entry (footprint and
  supplier part), and every net label landed on a pin.

To open it: in EasyEDA Standard, **File > Open > EasyEDA Source** and pick
`DualGreenHat_schematic.json`. EasyEDA Pro imports the same file (File >
Import > EasyEDA Standard). After a change to `build_schematic.py`:

```
python build_schematic.py
```

## PCB placement (EasyEDA)

[easyeda/build_pcb.py](easyeda/build_pcb.py) takes the parts and nets from
`build_schematic.py` and the LCSC footprints (cached in `easyeda/fp/`), and
places them from the `PLACE` table. It writes:

- `DualGreenHat_pcb.json`: an EasyEDA Standard PCB document with the board
  outline, three M3 holes, every footprint placed and every pad on its
  net. Power and coil nets get a 0.8 mm track rule ("Power"); the rest use
  0.254 mm.
- `placement.svg`: a top-view sketch for review.

The script also checks that no two footprints overlap and that nothing
leaves the board or crowds a mounting hole.

**Floorplan** (top view, 70 × 56 mm, y down):

| Area | Parts |
|---|---|
| Top edge | J101 (channels 1–3) and J201 (channels 4–6) on the left, a mounting hole, J1 (12 V in) on the right |
| Band below | the four DRV8313s in a row (U102, U103, U202, U203) |
| Around the drivers | charge-pump and VM caps above each driver, V3P3 and second VM caps below, 22 µF bulk per block |
| Right column | F1, D1, bulk caps, then the buck (U1, L1, R1–R4, C3–C8) |
| Bottom left | the Stamp-S3Bat sockets J2 (G-row, on top) and J3, with USB-C at the left board edge |
| Bottom middle | U101/U201 (74HCT86) and each channel's RC parts |

- **Drivers rotated 180°:** pins 1–14 (charge pump, VM, coil outputs) face
  the terminals, and pins 15–28 (logic inputs) face the XORs and the
  S3Bat.
- **The S3Bat's G-row:** it faces the logic, so the six input traces are
  short.
- **The buck:** it sits in the far corner from the module's USB end.

**Checked in EasyEDA Standard:** a test board with one of each rotated
footprint type loaded cleanly. EasyEDA drew the designators itself,
built the nets from the pad nets, applied the Power rule and kept each
part's LCSC number and BOM flag.

**Not included:** 3D models (EasyEDA can re-attach them by package),
copper pours, traces and vias.

**To check in EasyEDA before routing:**

- **Screw-terminal orientation:** the wire entry of J1, J101 and J201 must
  face the top board edge. Check in the 3D view and rotate 180° if not.
- **Where the S3Bat's antenna is:** keep copper pour (top and bottom)
  away from under that end of the module.
- **The module's outline:** 18 × 29.8 mm. It sits above J2/J3, so nothing
  tall may go under it. C107 and C207 sit just outside it.

## Next steps

1. Open `DualGreenHat_pcb.json` in EasyEDA (File > Open > EasyEDA
   Source) and do the checks under "PCB placement".
2. Route the power path by hand first:
   - J1 → F1 → D1 → the VM bus;
   - the buck, with its switching loop kept tight;
   - the coil outputs to J101/J201;
   - VM to each driver.
3. Add a GND pour on the bottom layer, with vias under each DRV8313's
   thermal pad (the footprint has them). Keep the pour clear of the
   module's antenna end.
4. Route the logic by hand or with the autorouter.
5. Run DRC, then export Gerbers, BOM and pick-and-place. Add the Gerbers
   and a schematic PDF to this folder, as the TAPR OHL requires.
6. Order a small batch; bench-test against PHASE1_BENCH.md.
