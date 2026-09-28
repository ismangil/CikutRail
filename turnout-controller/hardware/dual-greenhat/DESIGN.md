# Dual-GreenHat carrier board: design note

Status: **exploring**. Nothing drawn yet.

One PCB that the Stamp-S3Bat plugs into, carrying two copies of IoTT's
GreenHat Coil Driver circuit: six turnout channels for this layout's six
Kato turnouts. It replaces two GreenHat boards, the servo cables and the
loose 5VOUT/GND wiring described in [WIRING.md](../../docs/WIRING.md).

The firmware doesn't change. The board uses channels 1–6 (G1–G6); channels
7–11 stay available for layouts built from separate GreenHats.

## Decisions so far

| Topic | Decision |
|---|---|
| Channels | 6 (two GreenHat circuits of 3) |
| Turnouts | Kato Unitrack only: one two-wire coil per turnout |
| S3Bat mounting | **Stamp-S3Bat DIP** (S015-DIP, headers pre-soldered) in two 1×9 female 2.54 mm headers, S3Bat removable |
| Coil power input | One 5.08 mm 2-pin screw terminal, as on the GreenHat (KF128L-5.08-2P, LCSC C474940) |
| Turnout outputs | Small 2.54 mm screw terminals, **2 pins per turnout** (the GreenHat's KF128-2.54 family, 2-pin instead of 4-pin) |
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
TAPR OHL v1.0 too (not the repo's AGPL), with credit to IoTT. Add the
license file to this folder with the first design files.

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
- USB-C is on the short edge nearest pin 1 (G1 / 5VIN); the SH1.0 battery
  connector is on the opposite short edge. Put the USB-C end at the
  carrier's board edge so a cable still fits once the module is plugged
  in. The PWR button is on the module's top face and stays reachable.
- The module carries a 24-pin BTB connector and an FPC Wi-Fi antenna.
  Keep the area between the header rows free of tall parts, and keep
  copper pour and metal (screw terminals, the VM traces) away from
  under the module, to give the antenna room.

**Pinout** (top view, USB-C end at the top; pin 1 at the top of each row)

| Pin | Left row (J3) | Carrier use | Right row (J5) | Carrier use |
|---|---|---|---|---|
| 1 | G1 | ch1 | 5VIN | optional: 5 V from the carrier, see "Node power" |
| 2 | G2 | ch2 | 3V3 (3V3_L2) | optional: input pull-ups (see below) |
| 3 | G3 | ch3 | VBAT | not connected |
| 4 | G4 | ch4 | G8 | not connected |
| 5 | G5 | ch5 | G9 | not connected |
| 6 | GND | logic GND | G10 | not connected |
| 7 | **5VOUT** | VCC5.0 (GreenHat logic) | G11 | not connected |
| 8 | G6 | ch6 | WAKE (PY_G4_WAKE) | not connected |
| 9 | G7 | not connected (spare test pad) | GND | logic GND |

So the **left row alone** carries all six channels, 5VOUT and GND. The
right row is only needed for GND, the optional 3V3 pull-up rail and the
optional 5VIN feed; its other pins just hold the module in place.

**Power pins, from the schematic**

- **5VOUT** (EXT_5V_OUT): an SY7088 boost from the module's system rail,
  enabled by the PM1 (`5VOUT_EN`). It's off at power-on until the firmware
  turns it on, as the bench tests showed.
- **5VIN**: goes into the module's power path through an ideal-diode stage,
  the same as USB-C. So a 5 V supply on the carrier can power the node
  through this pin, and USB-C can stay plugged in for flashing. Don't feed
  it from the coil supply directly; it needs a regulated 5 V.
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
| 74HC86 | 2 | 2 (6 gates used, 2 spare). Consider **74HCT86**, see below |
| RC delay + 1 MΩ trimmer | 6 | 6 (unchanged) |
| Input pull-up + 100 nF | 6 | 6. Pull-up rail: see below |
| Steering diodes D1–D6 | 12 | **none** |
| Output terminals | 6 × 4-pin | **6 × 2-pin** |
| Power terminal + Q1/D7/R7 | 2 sets | **1 set** |
| H7350 LDO + J6 | 2 | **none**. VCC5.0 comes from the S3Bat's 5VOUT |
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

Do option 1 first; both need a quick check on the bench (a GreenHat with
its U3 swapped would do).

### Coil power

- One VM supply for all six channels, through a single Q1. The
  firmware's stagger scheduler fires one turnout at a time, so the peak
  is one coil.
- Kato Unitrack coils: confirm the voltage and current Kato specifies.
  The DRV8313 is rated to about 2.5 A peak per output; Q1 (SI2301) sets
  the board's continuous limit. Size the VM traces and the terminal for
  one coil's peak current plus margin.
- Bulk capacitance on VM near each DRV8313, as on the GreenHat (100 nF
  per VM pin), plus one bulk electrolytic or ceramic at the input.

### Outputs and turnout direction

Each 2-pin terminal carries `DRVn-0` / `DRVn-1`. A Kato coil moves one
way or the other depending on polarity, so a turnout wired backwards is
fixed by swapping its two wires or setting JMRI's "Inverted". Label the
pins on the silkscreen consistently, e.g. `1A/1B` … `6A/6B`.

## Channel map

| Channel | S3Bat pad | Driver block | Terminal |
|---|---|---|---|
| 1 | G1 | A, ch1 | T1 |
| 2 | G2 | A, ch2 | T2 |
| 3 | G3 | A, ch3 | T3 |
| 4 | G4 | B, ch1 | T4 |
| 5 | G5 | B, ch2 | T5 |
| 6 | G6 | B, ch3 | T6 |

All six are on the S3Bat's **left** row, together with 5VOUT and GND
(see "The Stamp-S3Bat DIP socket").

## Open questions

1. **S3Bat socket check.** The pinout and 15.24 mm row spacing come from
   M5Stack's schematic and drawing (see above). Before ordering, print
   the layout 1:1 and set the module on it to confirm pin 1, the
   orientation (USB-C end) and that the pins fit the chosen female
   headers.
2. **Node power.** Today the S3Bat runs from USB-C or a layout 5 V supply.
   The schematic shows 5VIN joins the module's power path like USB-C, so
   this board *could* make 5 V from the coil supply with a small buck
   converter (the H7350 LDO is too weak for Wi-Fi peaks) and feed
   right-row pin 1, so one screw terminal powers everything. Or keep 5 V
   separate and leave 5VIN unconnected?
3. **Kato coil figures** (voltage and current) for trace sizing and pulse
   length. The trimmer range (a few ms to about 5 s) is far longer than
   Kato needs.
4. **Trimmers at JLCPCB.** The 3386P is through-hole, so it needs JLC's
   through-hole assembly (extra cost) or hand soldering. An SMD trimmer
   of the same value is worth checking, and so is LCSC stock for every
   part at order time.
5. **Board outline and mounting:** size, mounting holes, whether it fits
   an enclosure.
6. **HCT86 / 3V3 pull-up change** (above): confirm on the bench before
   committing to it.

## Considered, not chosen

- **Firmware-timed pulses** (drive both half-bridges from GPIOs, no RC,
  XOR or trimmers): needs 12 GPIOs for 6 turnouts and a firmware change,
  and it throws away the GreenHat behaviour the firmware is built around.
  Keeping the GreenHat circuit means the board is a known design.

## Next steps

1. Answer open question 2 (it sets the power section).
2. In EasyEDA: import the GreenHat schematic, duplicate it into blocks
   A and B, then apply the changes in the table above.
3. Pick parts from LCSC (basic parts where possible) and check stock.
4. Lay out the board: power path and GND join first, then the drivers,
   the headers and terminals.
5. Order a small batch; bench-test against PHASE1_BENCH.md.
