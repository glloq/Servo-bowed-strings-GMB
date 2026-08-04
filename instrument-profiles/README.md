# Instrument profiles

Example configuration profiles for **Servo-bowed-strings-GMB**. Each file is
a full, valid instrument profile matching the JSON format of SPECIFICATION.md
§20 and the firmware `gmb::Profile` (`firmware/src/core/configuration/Profile.h`).

They are meant as realistic starting points: import one from the web interface,
then calibrate homing, fret positions, servos and the bow motors for your
physical build.

## Files

| File | Instrument | Strings | Tuning (MIDI) | Frets | Scale |
| ---- | ---------- | :-----: | ------------- | :---: | :---: |
| `violin-gdae.json` | Violin | 4 | G3 D4 A4 E5 — 55 62 69 76 | 19 | 328 mm |
| `viola-cgda.json` | Viola | 4 | C3 G3 D4 A4 — 48 55 62 69 | 19 | 370 mm |
| `cello-cgda.json` | Cello | 4 | C2 G2 D3 A3 — 36 43 50 57 | 14 | 690 mm |
| `doublebass-eadg.json` | Double Bass | 4 | E1 A1 D2 G2 — 28 33 38 43 | 9 | 1060 mm |

MIDI note reference: 60 = C4 (middle C). All four are tuned in **fifths** (the
double bass in **fourths**), the classic string-family tunings.

## What varies between examples

The four cover the standard bowed-string family, so they mainly exercise the
**register and scale** end of the schema:

* **Tuning / register** — from the violin (G3–E5) down to the double bass
  (E1–G2), four octaves apart, which drives each profile's `openNote` set and
  its announced MIDI range.
* **Scale length** — 328 mm (violin) → 370 mm (viola) → 690 mm (cello) →
  1060 mm (double bass); the fret geometry (§14.2) scales with it.
* **Playable frets** — 19 for violin/viola, 14 for cello, 9 for double bass,
  reflecting how far up the neck each is realistically stopped, which also sets
  `stringFretSelection.fret.maximum`.

They otherwise share the same mechanism: four strings, GT2-belt transmission,
identity string order (no reverse, no custom mapping), one finger + one bow per
string.

## Conventions shared by every profile

* **Pins** follow the recommended ESP32-S3-DevKitC-1 table (§11.5): `STEP` on
  4/5/6/7, `DIR` on 17/18/8/9, `HOME` on 12/13/14/21, I²C `SDA=40` / `SCL=41`,
  global `ENABLE=42`, PCA9685 `SERVO_OE=47`, and the bow H-bridge lines
  `BOW_PWM` on 1/2/10/11, `BOW_DIR` on 15/16/38/39, shared `BOW_EN=33`. The pins
  are written out explicitly **and** `board.automaticPinAssignment` is `true`, so
  the firmware re-derives the same conflict-free plan on import.
* **Servos on the PCA9685** — finger servos on channels `0 … N−1` and bow-press
  (`bowPress`) servos on channels `4 … 4+N−1`. Each `bowPress` servo carries a
  `minForceUs` floor (1300 µs) so a soft note still speaks.
* **One DC bow motor per string** — the `bows[]` array holds one `BowConfig` per
  string (duty range, PWM frequency, spin ramps, direction), driven through the
  H-bridge lines above.
* **One `strings[]` entry per string**, each with its own `homing` block.
* **Selection ranges track the instrument** — `stringFretSelection.string.maximum`
  equals the string count (4) and `.fret.maximum` equals the largest `maxFret`.
* `calibratedFretMm` is left empty (`[]`); positions are computed from the
  theoretical fret formula (§14.2) until you run manual calibration (§14.3),
  after which the calibrated table takes priority.

## Editing / validating

These are plain JSON. After editing, confirm the file still parses:

```sh
python3 -m json.tool instrument-profiles/violin-gdae.json > /dev/null
```

The firmware `ProfileValidator` performs the full semantic check (pin conflicts,
servo channel ranges, bow-config and selection bounds) when a profile is imported.
