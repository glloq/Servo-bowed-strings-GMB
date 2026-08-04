# Hardware — reference electronics

Reference electronics for **Servo-bowed-strings-GMB**, the ESP32-S3 MIDI
machine that drives one stepper-positioned finger per string **and bows each
string with its own DC motor** on bowed-string instruments — violin, viola,
cello, double bass (1–4 strings). This document describes the reference
architecture of SPECIFICATION.md §7; the wiring guide, bill of materials and
Phase 5 CAD deliverables live alongside it.

## Directory

```
hardware/
├── README.md          ← this file (electronics overview, §7)
├── BOM.md             ← bill of materials / nomenclature (§26)
├── wiring/
│   └── WIRING.md      ← connection guide, pinout, power rails (§7 / §22)
├── schematics/
│   └── README.md      ← Phase 5 placeholder (§24)
└── pcb/
    └── README.md      ← Phase 5 placeholder (§24)
```

## Block diagram (§7)

```text
                              Wi-Fi
                                │
                    MIDI + web configuration
                                │
                                ▼
                            ESP32-S3
                                │
     ┌───────────────┬──────────┼──────────┬───────────────┐
     │               │          │          │               │
STEP/DIR/EN   BOW_PWM/DIR/EN    I²C     Sensors        (safety cut)
     │               │          │          │
1–4 TMC2209    1–4 H-bridges  PCA9685  HOME / LIMIT
     │               │          │
1–4 steppers   1–4 bow motors  1–16 servos
                              (finger + bowPress)
```

## Major blocks

### Main controller — ESP32-S3 (§7.1)

The reference controller is an **ESP32-S3-DevKitC-1**. It handles Wi-Fi MIDI
transport, hosts the web configurator, allocates notes, plans motion, runs the
per-string state machines, drives the PCA9685, monitors the sensors, stores
profiles, and enforces safety. Its GPIO matrix lets peripheral signals be routed
to many pins, which is what makes the configurable board profiles and pin
assignment possible (`board-profiles/esp32-s3-devkitc-1.json`).

### Stepper drivers — 1–4 × TMC2209 (§7.2)

One STEP/DIR-compatible driver per string; the reference is the **TMC2209**.
Each axis exposes STEP, DIR, ENABLE and HOME, with optional LIMIT, DIAG and UART.
The first prototype board must accept **pluggable driver modules** so a driver
can be swapped, different models tried, motor current tuned, and maintenance
done before an integrated PCB exists.

Per-axis signals:

```text
STEP        (fast output from ESP32-S3)
DIR         (output)
ENABLE      (shared global ENABLE line, GPIO42 by default)
HOME        (reference sensor input, interrupt-capable)
LIMIT       (optional opposite end-stop)
DIAG        (optional TMC2209 stall/diag)
UART        (optional TMC2209 configuration)
```

### Bow motors — 1–4 × H-bridge (§7.2)

Each string is bowed by **its own DC motor** — a rosin-coated wheel or a linear
bow — driven through an **H-bridge** (e.g. a DRV8871 / TB6612 class part, one
channel per string). The bow presses onto the string via the per-string
`bowPress` descent servo, and the motor turns while the note is held, so the
string sounds **continuously** until Note Off.

Per-string bridge signals:

```text
BOW_PWM     (motor speed — LEDC PWM from the ESP32-S3, ~20 kHz ultrasonic)
BOW_DIR     (bowing direction — plain output; alternates for down-bow/up-bow)
BOW_EN      (shared ENABLE / STBY — ONE line disables every bridge at once)
VM / GND    (separate motor rail / common ground — see Power)
```

The single **`BOW_EN`** is tied into the hardware safety cut alongside the driver
`ENABLE` and the PCA9685 `/OE`, so a panic or E-stop kills every bow motor
instantly and independently of the firmware (§21). The bow motors run from a
**separate motor rail through the H-bridges**, exactly like the steppers run from
the 24 V rail.

### Servo expander — PCA9685 (§7.3)

A single **PCA9685** provides up to 16 servo channels over I²C. Recommended
channel map:

| Channels | Use |
| -------- | --- |
| 0–3 | finger press (one per string) |
| 4–7 | bow descent / contact force (`bowPress`, one per string) |
| 8–15 | auxiliary functions |

The PCA9685 `/OE` (output-enable) pin must be tied to a **safety GPIO**
(`SERVO_OE`, GPIO47 by default) so all servos can be neutralised instantly on
panic or emergency stop (§21).

### Sensors — HOME / LIMIT (§7.2, §13)

Each axis has a HOME reference sensor (mechanical, optical or Hall). LIMIT
opposite end-stops are optional (0–6). HOME/LIMIT inputs must land on
interrupt-capable GPIO with an appropriate pull (internal or external); the
homing state machine normalises the active level via `sensorActiveHigh`.

## Power (summary, §22)

Separate rails, servos and bow motors on **separate** supplies from the ESP32
regulator:

| Rail | Feeds |
| ---- | ----- |
| 24 V | stepper motors (via the drivers) |
| motor rail | bow DC motors (via the H-bridges) |
| 5–7.4 V | servomotors |
| 5 V | logic |
| 3.3 V | ESP32-S3 |

Fusing (stepper, bow-motor and servo rails), reverse-polarity protection, a TVS
on the motor rail(s), driver and H-bridge decoupling and a PCA9685 bulk capacitor
are required — see `wiring/WIRING.md` §Power.

## Capacity (§6)

| Resource | Min | Max |
| -------- | :-: | :-: |
| Strings / steppers / fingers / HOME sensors | 1 | 4 |
| Bow motors (H-bridge) | 1 | 4 |
| Opposite LIMIT switches | 0 | 4 |
| Finger servos | 1 | 4 |
| Bow-press servos | 1 | 4 |
| Auxiliary servos | 0 | 8 |
| Total servo outputs | 2 | 16 |

Invariant: **active strings = active stepper axes = movable fingers = bow
motors**, and each active string carries one finger servo **and** one mandatory
bow-press servo.
