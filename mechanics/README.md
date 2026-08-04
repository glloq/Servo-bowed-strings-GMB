# Mechanics — reference architecture

Reference mechanical architecture for **Servo-bowed-strings-GMB**
(SPECIFICATION.md §5), and how each mechanical parameter maps to the instrument-profile
fields (`firmware/src/core/motion/StepperAxis.h`, `instrument-profiles/`).

## 1. One independent channel per string (§5.1)

Every string is a self-contained channel. A stepper motor moves a **single**
finger along the string to select the note:

```text
Stepper motor
      ↓
Mechanical transmission
      ↓
Longitudinal carriage
      ↓
Single finger
      ↓
Position on the string
```

Hard invariant (§4, §6): **one movable finger per string, one stepper per
string, never shared**. Active strings = active stepper axes = movable fingers.

Per string the reference build carries (§1):

```text
1 stepper motor        1 finger-press mechanism (servo)
1 linear axis          1 bow (DC motor + rosined wheel / linear bow)
1 carriage             1 bow-press descent mechanism (servo)
1 single finger        1 HOME reference sensor
```

## 2. Finger press (§5.2)

Once the carriage is positioned, the finger must be able to press onto the
string, hold it, lift, stay lifted while moving, and stay lifted for an open
string. The reference mechanism is **one servo per string** (PCA9685 channels
0–5). In the profile this is a servo with `function: "finger"`, using `restUs`
(lifted) and `activeUs` (pressed), plus `travelMs`/`settleMs` timing.

Open string: finger stays lifted; the open string is bowed directly (§15.3). An
advanced option can instead press "fret 0" for specific mechanics.

## 3. Setting the string vibrating — the bow (§5.3)

Each string is set vibrating by **its own bow** — there is no shared exciter.
The bow is a **DC motor** turning a rosin-coated wheel (or driving a linear bow)
pressed onto the string. Unlike a plucked note, a bowed note sounds
**continuously**: the motor keeps turning, so the string keeps sounding until
Note Off.

Two mechanisms per string work together:

* **Bow motor** — a DC motor driven through an **H-bridge** (three lines:
  `BOW_PWM<n>` speed, `BOW_DIR<n>` direction, and one shared `BOW_EN`). MIDI
  velocity sets the motor speed (PWM duty); `BOW_DIR` can alternate each note for
  musical down-bow / up-bow strokes. Its parameters live in the profile's `bows`
  array (`BowConfig`) — see [`../docs/CALIBRATION.md`](../docs/CALIBRATION.md) §5.
* **Bow-press descent** — a **mandatory** per-string servo (`function: "bowPress"`)
  that lowers the bow onto the string and **holds the contact force**. Velocity
  scales the force between lifted (`restUs`) and full contact (`activeUs`), with a
  `minForceUs` floor so a soft note still speaks; CC7/CC11 re-drive it live.

So per string there are exactly two servo roles — `finger` (press) and
`bowPress` (bow descent / contact force) — plus the DC bow motor. There is no
pluck, strum or damper: the string is muted simply by lifting the bow and
stopping its motor at Note Off. Each string also has its own endstop: the `HOME`
reference sensor, plus an optional `LIMIT` switch at the far end.

Louder = **faster and firmer**: velocity drives the bow-motor speed and the
bow-press force together, and a held note can be shaped live with CC7/CC11 for
crescendo/decrescendo.

## 3.1 Servo signal source: PCA9685 or direct GPIO

Every servo picks its own source, so an instrument can be built **with or without
a PCA9685**, or with a mix of both:

* **PCA9685** — up to **four boards** (`pcaBoard` 0–3, I²C 0x40–0x43 = 64
  channels). Use this once you exceed the ESP32's free PWM pins.
* **Direct GPIO** — the servo hangs off a free ESP32-S3 pin (LEDC 50 Hz PWM),
  handy when there is no PCA or only a couple of servos. Direct-GPIO servos
  share the ESP32-S3's 8 LEDC channels with the bow-motor PWMs, so
  `direct-GPIO servos + bow PWMs ≤ 8`; a PCA9685 servo is off that budget.

The web interface exposes this choice per servo and prevents channel/pin
conflicts (see [`../docs/CALIBRATION.md`](../docs/CALIBRATION.md) §4).

## 4. Transmission options (§5.1)

The carriage can be driven by any of:

* **GT2 belt** (`transmission: "beltGt2"`)
* **Trapezoidal / ball lead screw** (`transmission: "screw"`)
* **Rack and pinion** — model via `custom`
* **Cable drive** — model via `custom`
* **Experimental** — model via `custom`

## 5. The millimetre abstraction

The firmware never thinks in the transmission's native units. Everything happens
in **millimetres**, converted to motor steps by a transmission-dependent
`stepsPerMm` factor (§5.1, §12.1):

```text
position in millimetres → conversion → position in motor steps
```

This keeps note allocation, motion planning and the profiles independent of the
mechanical type. Theoretical fret positions come from the equal-temperament
formula (§14.2):

```text
position(fret) = scaleLengthMm × (1 − 2^(−fret / 12))
```

A calibrated table (`calibratedFretMm`) overrides theory when present (§14.3).

## 6. Parameter → profile-field mapping

`stepsPerMm` is computed from the transmission (SPECIFICATION.md §12.1):

**Belt (GT2):**

```text
stepsPerMm = (stepsPerRevolution × microsteps) / (pulleyTeeth × beltPitchMm)
```

**Screw:**

```text
stepsPerMm = (stepsPerRevolution × microsteps) / leadPerRevolutionMm
```

**Custom:** use `customStepsPerMm` directly.

| Mechanical quantity | Profile field (`strings[]`) | Used when |
| ------------------- | --------------------------- | --------- |
| Vibrating (scale) length | `scaleLengthMm` | always (fret geometry) |
| Transmission type | `transmission` (`beltGt2`/`screw`/`custom`) | always |
| Motor full steps per revolution | `stepsPerRevolution` (e.g. 200 for 1.8°) | always |
| Driver microstepping | `microsteps` (e.g. 16) | always |
| Pulley tooth count | `pulleyTeeth` | belt |
| Belt tooth pitch | `beltPitchMm` (GT2 = 2 mm) | belt |
| Screw lead per revolution | `leadPerRevolutionMm` (e.g. 8 mm) | screw |
| Explicit steps/mm override | `customStepsPerMm` | custom |
| Direction sense | `invertDirection` | always |
| Soft travel limits | `minPositionMm`, `maxPositionMm` | always |
| Motion profile | `maxSpeedMmS`, `maxAccelMmS2` | always |
| Open-string note | `openNote` (MIDI) | always |
| Highest reachable fret | `maxFret` | always |
| Calibrated fret table | `calibratedFretMm[]` (index = fret) | overrides theory |

### Worked example (GT2 belt)

`stepsPerRevolution = 200`, `microsteps = 16`, `pulleyTeeth = 20`,
`beltPitchMm = 2`:

```text
stepsPerMm = (200 × 16) / (20 × 2) = 3200 / 40 = 80 steps/mm
```

At 16 microsteps and 80 steps/mm the position resolution is 1/80 mm = 12.5 µm,
comfortably finer than fret spacing on every example instrument.

## 7. Homing (§13) — mechanical reference

Each axis references itself against its HOME sensor with a non-blocking state
machine (`CHECK_SENSOR → SEEK_FAST → BACKOFF → SEEK_SLOW → SET_ZERO →
MOVE_TO_OFFSET → READY`). Mechanically relevant profile fields per string
(`strings[].homing`):

| Field | Meaning |
| ----- | ------- |
| `direction` | travel direction toward the sensor (+1 / −1) |
| `fastSpeedMmS` / `slowSpeedMmS` | seek and re-approach speeds |
| `backoffMm` | retreat distance after the first trigger |
| `offsetMm` | final resting offset past zero |
| `timeoutMs` / `maxSearchMm` | fault guards |
| `sensorActiveHigh` | electrical active level (NO/NC support) |

A failing axis is disabled without disturbing the others (§13.2).
