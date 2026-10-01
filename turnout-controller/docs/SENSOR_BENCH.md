# Sensor experiment: loco stopping point

An experiment, not a firmware phase. It uses phase 6's sensor mode
(no code changes) to detect a loco stopped at one station, on N-scale
Kato Unitrack.

## Approach

A small magnet on the loco's underside, and a reed switch or Hall sensor
(the Waveshare Hall Sensor module is the one being tested)
under the track. The magnet's field passes through the opaque Unitrack
roadbed, so nothing is cut and room light doesn't matter.

Reflective IR (TCRT5000, usable range about 0.2-15 mm, peak about
2.5 mm) was rejected for this track: the roadbed is opaque, N-scale
underside clearance is tiny, and the cheap modules don't modulate their
emitter, so ambient light matters. ToF modules (VL53L0X, minimum range
about 30-40 mm) are unsuitable under the track and need I²C, which the
turnout node's pins don't offer.

Loco only: one magnet on one loco. Other stock isn't detected.

## Parts

| Part | Notes |
|---|---|
| Reed switch (glass, small) | Simplest: a contact to GND, no power, polarity doesn't matter. Fragile; keep leads short. |
| or Waveshare Hall Sensor module (chosen for the test) | See "Waveshare Hall Sensor module" below. |
| Neodymium disc magnet | About 3 × 1 mm or 2 × 1 mm. Glue under the loco, away from the motor, wheels and pickups. |
| 1 kΩ resistor | In series with the signal wire, as in WIRING.md. |

## Wiring

On the bare Stamp-S3Bat, channel 7 (**G7**, left edge). G1-G6 stay for
turnouts.

- Reed: one leg to G7 through 1 kΩ, the other to GND.
- Waveshare Hall module: VCC to **3V3**, GND to GND, DOUT to G7 through
  1 kΩ. Leave AOUT free (or on a multimeter, see below).
- Never connect a 5 V output directly to the pin, and never use a
  channel wired to a GreenHat.

Node config (config page → Channels): channel 7 **sensor**, **pull-up**,
**active LOW**, name `107`. In JMRI the sensor is `MS107`.

## Waveshare Hall Sensor module

From the [wiki](https://www.waveshare.com/wiki/Hall_Sensor) and the user
manual:

- **49E linear Hall sensor** plus an **LM393 comparator**, a trimmer to
  set the sensitivity, and a signal LED. 2.3-5.3 V supply, so 3V3 works.
- The 49E is *linear and bipolar*, not a switch: AOUT sits at half the
  supply with no field (about 1.65 V at 3.3 V) and moves up or down with
  the field's strength and polarity. DOUT is the comparator's output
  against the trimmer setting, so one polarity triggers it and the other
  probably doesn't (test).
- The LED lights near a magnet. The manual doesn't say which level DOUT
  has then. Typical LM393 modules pull DOUT LOW, which would mean *Active
  LOW*; measure it.
- The manual lists 29.2 × 11.2 mm and the wiki 27.0 × 15.5 mm (different
  revisions). Measure yours. The 49E is the small black TO-92 part on the
  board's edge; its flat face is the sensing side, so aim that face at the
  magnet and put that part under the stopping point.
- **Magnet: 2 × 1 mm N52 disc.** Estimated on-axis field and AOUT
  shift, from the disc-magnet formula (Br about 1.45 T) and the 49E's
  typical 1.4 mV/gauss scaled to a 3.3 V supply. These are estimates, not
  measurements; the sensitivity isn't confirmed for this module.

  | Gap (magnet face to the 49E) | Field | AOUT shift |
  |---|---|---|
  | 2 mm | ~390 G | ~360 mV |
  | 3 mm | ~155 G | ~145 mV |
  | 4 mm | ~75 G | ~70 mV |
  | 5 mm | ~42 G | ~40 mV |
  | 6 mm | ~26 G | ~24 mV |
  | 8 mm | ~12 G | ~11 mV |

  Aim for 4-5 mm or less. Beyond about 8 mm the shift is near noise and
  the comparator's offset; then use a bigger disc (for example 3 × 1.5 mm
  or 5 × 2 mm). The 49E sits a little inside its package, so the real
  gap is larger than the distance to the board edge, and it includes the
  roadbed and the loco's ground clearance. The field also falls off fast
  sideways, so at a 4 mm gap the loco may trigger only within a few mm of
  the sensor. A lower threshold widens that window but leaves less margin.
  The loco's chassis and motor may distort the field; test with the
  magnet in place. Measure the real figures on the bench and replace the
  table.
- AOUT isn't used by the firmware (digital input only), but is useful on
  the bench: read it with a multimeter to see the field at each gap and
  to set the trimmer midway between the "magnet present" and "magnet
  away" readings.

## Trackside option: Pololu digital distance sensor v2, 5 cm (#5460)

Also on test, beside the Hall sensor. From the
[Pololu product page](https://www.pololu.com/product/5460):

- A small **lidar (VCSEL time-of-flight)** module with one digital
  output: **LOW when an object is within range, HIGH otherwise**, driven
  push-pull to the supply level. It says only *if* something is in range,
  not how far.
- 3.0-5.5 V supply, **30 mA typical**, 940 nm, update 142 Hz or better,
  field of view about 10° (varies with reflectivity and light), detects
  from under 1 mm out to 5 cm. Largely independent of reflectivity and
  ambient light, though range drops for very dark objects.
- **Hysteresis:** the output goes LOW at 50 mm or closer and back HIGH
  only beyond 85 mm. Anything left within 85 mm after a detection (a
  wall, a building) keeps it LOW, and anything within 50 mm of the face
  holds it LOW from the start. Keep the line of sight clear.
- **Remove the protective liner** over the sensor IC before testing.
- Side-entry 3-pin JST SH connector: pin 1 GND (black), pin 2 VIN (red),
  pin 3 OUT (white). Size 22.9 × 8.9 × 5.2 mm, one M2 mounting hole.
- The jumpers set the variant; this one is digital 5 cm (0000). Solder
  bridges can change it, for example 10 cm (0010) or the
  higher-sensitivity versions (21 mm hysteresis, 66 Hz).

**Wiring:** VIN to **3V3**, GND to GND, OUT to **G8** through 1 kΩ. Do
**not** power it from 5VOUT: the output swings to the supply level, which
would put 5 V on the pin. The 3V3 pad's current limit isn't checked here;
30 mA should be fine, but confirm. Node config: channel 8 **sensor**,
pull-up, **active LOW**, name `108` (`MS108`). The output is push-pull, so
the pull-up is harmless but not needed.

**Mounting:** the sensor looks sideways at the loco from beside the track.
The 10° beam is a spot of about 0.175 × the distance (roughly 5 mm wide
at 30 mm), so it marks a narrow position. It needs a clear opening in the
platform edge or a building, with nothing behind within 85 mm. The loco
body must come within 5 cm of the face; the sensor can sit at the
roadbed edge.

**Extra tests:**

1. Short G8 to GND: `sensor 108 (ch8) ACTIVE`.
2. With a hand, then the loco, find the distance where OUT goes LOW (up to
   5 cm) and where it returns HIGH (85 mm). Try the loco's colours and
   any dark stock.
3. Slide the loco along the track and note the position window: how far
   along the track the beam sees the loco.
4. Check nothing nearby holds it LOW, with the track empty.
5. Run the same JMRI stop test (`MS108`) and compare the stop position
   with the Hall sensor.

## Mounting

- Put the sensor against the underside of the roadbed at the stopping
  point, in a pocket in the baseboard if the track sits flat.
- The gap is roadbed thickness plus loco ground clearance (a few mm).
- Align the loco's magnet over the sensor at the nose position you
  want. Mark the Hall magnet face that triggers.

## Timing

The sensor is debounced 50 ms, then MQTT, then JMRI. Allow a few hundred
milliseconds. At 20 scale km/h an N-scale loco moves about 35 mm/s, so
300 ms is about 10 mm. Approach slowly, or place the sensor slightly
before the target. A loco passing at speed may not register (shorter
than the debounce); a stopped loco stays ACTIVE.

## Tests

1. **Bench trigger.** Set channel 7 as above. Short G7 to GND: `status`
   shows `sensor 107 (ch7) ACTIVE`, and `tools/mqtt_exercise.py watch`
   shows `track/sensor/107` = `ACTIVE`, retained. Release: `INACTIVE`.
2. **Sensor alone.** Wire the reed or Hall. Hold the magnet over it by
   hand at the real gap: ACTIVE; remove: INACTIVE. For the Waveshare
   module: note the LED and DOUT level with the magnet near and away
   (set *Active LOW* to match), read AOUT with a multimeter, flip the
   magnet to find the triggering face, then set the trimmer midway.
3. **Gap.** On a spare Unitrack piece, with the magnet on the loco, find
   the largest gap that still triggers, and the position window along
   the track (how far the loco can move before it drops out).
4. **Track effect.** Check the magnet isn't pulled to the rails and
   doesn't affect the loco's motor or pickups.
5. **Pass vs stop.** Run the loco over at slow, medium and full speed:
   note whether each registers. Stop on the sensor: stays ACTIVE.
6. **JMRI.** Add `MS107` (Tools → Tables → Sensors, hardware address
   `107`). It follows the loco.
7. **Stop position.** Have JMRI stop the loco on `MS107` ACTIVE.
   Measure where the loco ends up relative to the target at a few
   approach speeds; move the sensor to compensate.
8. **Reboot.** Power-cycle the node: the sensor republishes its state.

## Results

| Test | Result | Notes |
|---|---|---|
| 1 Bench trigger | | |
| 2 Sensor alone | | |
| 3 Gap | | |
| 4 Track effect | | |
| 5 Pass vs stop | | |
| 6 JMRI | | |
| 7 Stop position | | |
| 8 Reboot | | |

## Open questions

- Hall (under the track, needs a magnet) or the Pololu sensor (trackside,
  no loco change): which gives the steadier, more precise stop?
- Reed or the Waveshare Hall module: which is more reliable at the real
  gap, and is a 3 × 1 mm magnet strong enough for it?
- Does a second sensor before the station (channels 8-11 are free) help
  slow the loco in time?
