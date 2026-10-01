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
- A small magnet is weak: my rough estimate for a 3 × 1 mm N52 disc is
  tens of gauss at a few mm, a change of tens of mV on AOUT. If the
  trimmer can't separate it from noise, use a bigger magnet (for example
  5 × 2 mm) or a smaller gap. The wiki's own demo reads about 2.2-2.4 V
  on AOUT with a magnet very close.
- AOUT isn't used by the firmware (digital input only), but is useful on
  the bench: read it with a multimeter to see the field at each gap and
  to set the trimmer midway between the "magnet present" and "magnet
  away" readings.

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

- Reed or the Waveshare Hall module: which is more reliable at the real
  gap, and is a 3 × 1 mm magnet strong enough for it?
- Does a second sensor before the station (channels 8-11 are free) help
  slow the loco in time?
