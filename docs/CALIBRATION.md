# Calibration procedure — Servo-bowed-strings-GMB

> Sources: `SPECIFICATION.md` §12, §13, §14, §15 · Code: `core/motion/{StepperAxis.*, HomingController.*}`, `core/Types.*`, `core/configuration/Profile.h`.
> Related documents: [`WEB_INTERFACE.md`](WEB_INTERFACE.md) (wizard §10) · [`SAFETY.md`](SAFETY.md) · [`FIRST_CONFIGURATION.md`](FIRST_CONFIGURATION.md).

This document describes calibration: motor steps/mm, homing, fret positions
(theoretical and manual), and servos.

---

## 1. Assisted steps/mm calculation (§12.1)

The firmware works in **millimeters** and converts to motor steps via a
`steps/mm` factor that depends on the transmission (`StepperAxis::stepsPerMm()`),
which abstracts the mechanical type away from the rest of the code.

```cpp
enum class Transmission { BeltGt2, Screw, Custom };
```

### 1.1 Belt (GT2)

```text
                stepsPerRevolution × microsteps
stepsPerMm = ──────────────────────────────────
                    pulleyTeeth × beltPitch
```

Example: 1.8° motor (200 steps/rev), 16 microsteps, 20-tooth pulley, GT2 pitch
2 mm → `(200 × 16) / (20 × 2) = 80 steps/mm`.

### 1.2 Screw

```text
                stepsPerRevolution × microsteps
stepsPerMm = ──────────────────────────────────
                    leadPerRevolution
```

Example: 200 steps/rev, 16 microsteps, screw with 8 mm/rev lead → `3200 / 8 = 400 steps/mm`.

### 1.3 Custom value

`Custom` transmission: `customStepsPerMm` is used directly.

Relevant parameters in `AxisConfig`: `stepsPerRevolution`, `microsteps`,
`pulleyTeeth`, `beltPitchMm`, `leadPerRevolutionMm`, `customStepsPerMm`,
`invertDirection`, `minPositionMm`/`maxPositionMm`, `maxSpeedMmS`, `maxAccelMmS2`.
Conversions: `mmToSteps(mm)`, `stepsToMm(steps)`, clamping `clampToLimits(mm)`.

---

## 2. Homing (§13)

Homing is **non-blocking** and **independent** for each string
(`HomingController`, one instance per axis). On each tick it reads the sensor and
the position, and returns the motion command to apply (`HomingCommand`).

### 2.1 State machine

```text
Idle → CheckSensor → SeekFast → (SensorDetected) → Backoff →
SeekSlow → SetZero → MoveToOffset → Ready
                                       └─(fault)─► Fault
```

| State (`HomingState`) | Role |
| -------------------- | ---- |
| `Idle` | inactive |
| `CheckSensor` | verify the sensor is not already active |
| `SeekFast` | fast approach toward the sensor (`fastSpeedMmS`) |
| `Backoff` | back off after detection (`backoffMm`) |
| `SeekSlow` | slow, precise re-approach (`slowSpeedMmS`) |
| `SetZero` | set the origin |
| `MoveToOffset` | move to the rest offset (`offsetMm`) |
| `Ready` | axis ready |
| `Fault` | axis disabled |

Configuration (`HomingConfig`): `direction` (±1 toward the sensor), `fastSpeedMmS`,
`slowSpeedMmS`, `backoffMm`, `offsetMm`, `timeoutMs` (default 8000), `maxSearchMm`
(default 500), `sensorActiveHigh` (the raw electrical level is normalized
internally).

### 2.2 Detected faults (§13.2, `HomingFault`)

| Fault | Cause |
| ------ | ----- |
| `SensorActiveAtStart` | sensor active at startup |
| `SensorNotReleased` | sensor cannot be released |
| `SensorNeverReached` | sensor never reached |
| `Timeout` | timeout exceeded |
| `MaxDistanceExceeded` | maximum search distance exceeded |
| — | inconsistent activation of HOME and LIMIT (detected upstream) |

A faulty axis is disabled **without causing any unexpected movement** on the
other axes.

### 2.3 Parallel homing (§13.1)

Options: **simultaneous**, **sequential** (if power supply is limited), or **by
groups**. Each axis keeps its own `HomingController` instance.

---

## 3. Note / fret calibration (§14)

### 3.1 Tuning

Each string has: open MIDI note (`openNote`), maximum fret included (`maxFret`),
and the position of each fret. Predefined tunings are provided (violin, viola,
cello, double bass, custom), all fully editable. The instrument is musically
fretless — the bow excites the string continuously — but the stepper still
selects **discrete semitone positions** along the string, so the same
fret-position model applies.

### 3.2 Theoretical calculation (§14.2)

```text
position = scale length × (1 − 2^(−fret / 12))
```

Implemented in `core/Types.cpp` (`fretPositionMm(scaleLengthMm, fret)`) and
exposed by `StepperAxis::fretPositionMm(fret)`. `scaleLengthMm` = the string's
vibrating length. The note produced at a fret: `note = openNote + fret + capo + transpose`.

Example (length 330 mm): fret 12 → `330 × (1 − 2^(−1)) = 165 mm` (octave at the
middle of the string).

### 3.2a Position relative to the FDC — per-string offset

The theoretical spacing and the calibrated table are both measured **from the nut**
(fret 0 = 0). Each string then carries a single **`fretOffsetMm`** — the distance
from its HOME endstop (the FDC) to the nut — and the axis target is:

```text
absolute position (from FDC) = fretOffsetMm + (calibrated[fret]  or  theory(fret))
```

So `fretOffsetMm` places a whole fretboard relative to its FDC and shifts every
fret of that string at once; it is decoupled from the travel limit `minPositionMm`
(which is a pure clamp). In the web editor the fret table is entered nut-relative,
an **Abs (FDC)** column shows `fretOffsetMm + value`, and **Capture position**
records the live motor position (absolute) minus the offset, so it stores a
nut-relative value that stays correct if the offset is later changed.

### 3.3 Manual calibration (§14.3)

For each fret: (1) select the fret, (2) move the motor with buttons, (3) test the
note, (4) adjust the position, (5) save the exact position. **The calibrated
table takes priority over the theoretical position**: if
`AxisConfig::calibratedFretMm[fret]` is set, `fretPositionMm()` returns the
calibrated value rather than the theoretical one.

### 3.4 Compensation (§14.4)

The system allows: individual correction of a fret, correction according to the
direction of travel (mechanical play / backlash), a global offset for the string,
and forward and backward software limits (`minPositionMm` / `maxPositionMm`,
applied by `clampToLimits`).

---

## 4. Servo calibration (§15)

Each servo uses pulses calibrated in microseconds (`ServoConfig`):

```cpp
enum class ServoSource : uint8_t { Pca = 0, DirectGpio = 1 };

struct ServoConfig {
    bool enabled;
    std::string function;         // "finger"/"bowPress"/"aux"
    int8_t stringIndex;           // owner string, -1 = shared/global

    ServoSource source;           // PCA9685 OR direct ESP32 GPIO
    uint8_t pcaBoard;             // 0..3 : up to four PCA9685 (0x40..0x43)
    uint8_t channel;              // PCA9685 channel 0..15   (source == Pca)
    int8_t  gpio;                 // ESP32 GPIO            (source == DirectGpio)

    uint16_t pulseMinUs, pulseMaxUs;
    uint16_t restUs, activeUs;    // finger up / bow lifted  ·  finger down / bow at full force
    bool inverted;
    uint16_t travelMs;            // travel time
    uint16_t settleMs;            // settle time
    bool disableAtRest;           // disable at rest

    // Continuous-contact shaping (bow pressure).
    uint16_t minForceUs;          // minimum pulse toward the active side so a soft
                                  // note still makes contact (0 = velocity-only)
};
```

### 4.0a Bow contact force (`bowPress`)

The `bowPress` servo does **not** strike-and-return like a plectrum: it lowers
the bow onto the string and then **holds** a contact force for as long as the
note sounds. MIDI velocity scales that force between `restUs` (bow lifted off)
and `activeUs` (full contact force); CC7/CC11 re-drive it live on a held note.
One extra field shapes the gesture:

* **`minForceUs`** — a floor on the pulse toward the active side so a soft
  (low-velocity) note still presses the bow hard enough to make the string
  speak. `0` disables it (the force follows velocity only). The shipped bowed
  profiles set it to `1300 µs` on every `bowPress` servo.

The string is set in vibration by the **DC bow motor**, not by this servo — its
speed lives in the per-string `BowConfig` (see §5), while the `bowPress` servo
here sets only the pressure of the bow against the string.

### 4.0b Playback timing / latency (global, `MidiConfig`)

Three global knobs manage the delay between a MIDI Note On and the sound, and how
much the mechanics anticipate to keep that delay small:

* **`noteExecutionDelayMs`** — a **fixed** delay from Note On reception to the note
  actually sounding. The carriage move, finger press and bow prep all happen
  inside this window, so the note plays at a predictable, constant latency
  (`reception + delay`) as long as the mechanics can be ready in time. `0` = play
  as soon as ready (variable latency).
* **`fingerLeadMs`** — begin the finger descent up to this long **before** the
  carriage is estimated to reach the fret, so the finger arrives on the string
  around arrival instead of only starting to descend then. Set too large it can
  drag the finger during the slide, so it is an opt-in value to tune on the bench
  (`0` = press only after arrival, the safe default).
* **`bowLeadMs`** — begin lowering the bow (the `bowPress` descent servo) up to
  this long **before** the string becomes ready, so the bow wheel is already in
  contact when the note is due and the motor spin-up (`BowConfig.spinUpMs`) can
  overlap the last of the approach. `0` = lower the bow only once the string is
  ready.

`fingerLeadMs` and `bowLeadMs` shrink the *minimum* achievable
`noteExecutionDelayMs`; all three default to `0` (strictly sequential, safe).

### 4.0 Signal source: PCA9685 or direct GPIO

The system works **with or without a PCA9685**. Each servo independently chooses
its source:

* **PCA9685** — up to **four boards** (`pcaBoard` 0–3, addresses 0x40–0x43),
  i.e. **64 channels** in total; each servo indicates its board and its `channel`
  (0–15). Ideal when the number of servos exceeds the free PWM pins.
* **Direct GPIO** — the servo is driven by a free pin on the ESP32-S3
  (LEDC PWM 50 Hz). Useful without a PCA or for just a few servos. Direct-GPIO
  servos share the ESP32-S3's **8 LEDC channels with the bow-motor PWMs**, so
  `direct-GPIO servos + bow PWMs ≤ 8` (see [`PIN_CONFIGURATION.md`](PIN_CONFIGURATION.md));
  a PCA9685 servo uses the expander's own PWM and is off that budget.

The two modes can be **mixed** on the same instrument. The validator rejects: a
PCA channel (board + channel) used twice, a direct GPIO that is reserved or in
conflict with a stepper/bow signal or another servo, and a per-string role
pointing to a nonexistent string.

### 4.1 Per-string roles

Each string (1 to 4) has exactly two servos: a `finger` and a **mandatory**
`bowPress`.

| Role       | Function                                                    |
| ---------- | ----------------------------------------------------------- |
| `finger`   | finger press on the fret (selects the pitch)                |
| `bowPress` | lowers the bow onto the string and sets the contact force   |

**Shared** roles (`aux`, `stringIndex = -1`) allow an auxiliary mechanism that
spans several strings. There is no pluck, strum or damper role on a bowed
instrument: the string is excited by its DC bow motor (see §5), and it is muted
simply by lifting the bow and stopping the motor at Note Off.

### 4.1 Finger (§15.1)

Lifted / pressed positions, delay after pressing, delay after releasing. The open
string (fret 0): finger lifted, motor possibly in a safe position, the open
string is bowed directly.

### 4.2 Bow descent — `bowPress` (§15.2)

Lifted (`restUs`) and full-force (`activeUs`) positions, travel/settle timing,
and `minForceUs` (the minimum-force floor). Velocity scales the held pressure
between rest and active; CC7/CC11 re-drive it live on a sounding note. The bow is
lowered at Note On and lifted at Note Off — it does not oscillate.

### 4.3 Open string (§15.3)

Finger lifted, motor possibly moved to a safe position, the open string is bowed
directly. Advanced option: use the finger on the zero fret for a specific
mechanism.

Recommended layout on a first PCA9685 board (16 channels): 0–3 finger presses,
4–7 bow-descent (`bowPress`) servos, 8–15 auxiliaries. Beyond that, add boards
(`pcaBoard` 1–3) or servos on direct GPIO. Each PCA9685's `/OE` output is wired
to a safety pin — see [`SAFETY.md`](SAFETY.md).

---

## 5. Bow drive calibration (`BowConfig`)

Each string is bowed by **its own DC motor** — a rosin-coated wheel or a linear
bow — driven through an H-bridge. The bridge takes three lines: a PWM output
(`BOW_PWM<n>`, speed), a DIR output (`BOW_DIR<n>`, bowing direction) and one
shared ENABLE (`BOW_EN`) that disables every bridge at once (pins in
[`PIN_CONFIGURATION.md`](PIN_CONFIGURATION.md)). The motor turns while the note is
held, so the string sounds **continuously** until Note Off. One `BowConfig` per
string is stored in the profile's `bows` array:

```cpp
struct BowConfig {
    bool enabled;
    int8_t stringIndex;         // owning string

    uint8_t  minDutyPct;        // lowest duty that reliably keeps the string bowing (default 25)
    uint8_t  maxDutyPct;        // duty at full velocity (default 100)
    uint16_t pwmFreqHz;         // H-bridge PWM frequency (default 20000, ultrasonic → inaudible)

    uint16_t spinUpMs;          // rest → target ramp at note start (default 40)
    uint16_t spinDownMs;        // target → rest ramp at note off (default 60)

    bool reverse;               // invert the default DIR level (bowing direction)
    bool alternate;             // alternate DIR each note (down-bow / up-bow)
};
```

Calibration notes:

* **Speed vs. velocity.** MIDI velocity maps the PWM duty between `minDutyPct`
  and `maxDutyPct`. Raise `minDutyPct` until the softest note speaks cleanly;
  lower `maxDutyPct` if a loud note over-drives or squeals. CC7/CC11 re-drive the
  duty live on a held note, so this range also sets the crescendo span.
* **`pwmFreqHz`** defaults to **20 kHz** so the switching itself is ultrasonic
  and inaudible; the motor hears only the average voltage.
* **Spin ramps.** `spinUpMs` / `spinDownMs` smooth the attack and release: a
  motor cannot start or stop instantly, and ramping avoids a click. `spinUpMs`
  overlaps the last of the approach when `bowLeadMs` (§4.0b) is set.
* **Direction.** `reverse` flips the resting bowing direction; `alternate`
  swaps `BOW_DIR<n>` on each successive note for musical **down-bow / up-bow**
  strokes.

Louder therefore means **faster and firmer**: velocity drives both the bow
duty here and the `bowPress` contact force (§4.0a) together. During homing the
bows stay disabled; `BOW_EN` is asserted only once the instrument is armed
(Ready) — see [`SAFETY.md`](SAFETY.md).
