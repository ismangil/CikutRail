# Sensor experiment: loco stopping point

An experiment, not a firmware phase. It uses phase 6's sensor mode
(no code changes) to detect a loco stopped at one station, on N-scale
Kato Unitrack.

## Approach

A small magnet on the loco's underside, and a reed switch or Hall sensor
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
| or unipolar Hall switch, open-collector | A3144 needs about 4.5 V, so power it from **5VOUT**; its output only pulls down, so the pin never exceeds 3.3 V. A 3.3 V part such as TI DRV5032 (TO-92) should also work. Check the datasheet. Unipolar: only one magnet face triggers it. |
| Neodymium disc magnet | About 3 × 1 mm or 2 × 1 mm. Glue under the loco, away from the motor, wheels and pickups. |
| 1 kΩ resistor | In series with the signal wire, as in WIRING.md. |

## Wiring

On the bare Stamp-S3Bat, channel 7 (**G7**, left edge). G1-G6 stay for
turnouts.

- Reed: one leg to G7 through 1 kΩ, the other to GND.
- Hall: VCC to 5VOUT, GND to GND, output to G7 through 1 kΩ.
- Never connect a 5 V output directly to the pin, and never use a
  channel wired to a GreenHat.

Node config (config page → Channels): channel 7 **sensor**, **pull-up**,
**active LOW**, name `107`. In JMRI the sensor is `MS107`.

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
   hand at the real gap: ACTIVE; remove: INACTIVE. For a Hall, flip the
   magnet and note which face triggers.
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

- Reed or Hall: which is more reliable at the real gap?
- Does a second sensor before the station (channels 8-11 are free) help
  slow the loco in time?
