# Servo-bowed-strings-GMB

**Turn a real bowed-string instrument into a MIDI-controlled robot.**

Send it MIDI notes over Wi-Fi and it bows a violin, viola, cello or double bass
for you — a stepper motor slides a "finger" along each string to pick the note, a
small servo lowers a motor-driven **bow wheel** onto the string, and the bow
keeps the note singing for as long as you hold it. Everything is configured from
a **web page in your browser**; no app to install.

> Built for the **ESP32-S3**. The brain is a portable, unit-tested C++ core; the
> ESP32 part is just the hardware glue. Adapted from the sister project
> **Stepper-Plucked-Strings-GMB** — same architecture, a bow instead of a pluck.

---

## How it works

One **string** = one **stepper axis** + one **bow motor** + two **servos**:

```
        ┌──────────────────── one string ────────────────────┐

   nut                      moving finger                   bridge
    │                            ▼                             │
    ╞════════════════════════════●═════════════════════════════╡  ← the string
    0    1    2    3    4    5   fret positions (mm)
    │                            ▲                             │
    │                     stepper motor slides                │
    │                     the finger to the fret              │
    │                                                          │
    │                 ╭─ bow descent servo lowers the ─╮       │
    │                 ▼   spinning bow onto the string  ▼      │
    │            ( bow wheel )  ← DC motor via H-bridge        │
    └─ servos: [finger] press the note   [bowPress] set the force ┘
```

To play a note the firmware:

1. **moves** the carriage so the finger sits at the right fret,
2. **presses** the finger with a servo,
3. **lowers the bow** with the descent servo and **spins the bow motor**,
4. **keeps bowing** — the note sustains continuously —
5. **stops the motor and lifts the bow** when the note ends.

Up to **4 strings** run independently and in parallel, so it can play chords and
hold them.

### What makes a bow different from a pluck

A plucked note is a single strike that decays on its own. A bowed note is
**driven continuously**, so the firmware treats it as a sustained excitation:

- **Velocity** sets *both* the bow speed (the H-bridge motor's PWM duty) *and*
  the contact force (the descent servo's position) — louder is faster **and**
  firmer.
- **CC7 (volume)** and **CC11 (expression)** modulate a note **while it is still
  sounding** — real **crescendo / decrescendo** and phrasing, not just the next
  attack.
- **Note Off** ramps the motor down and lifts the bow; there is no pluck to fire
  and no ring-out to damp.

### The signal path

```
MIDI over Wi-Fi ─▶ parse ─▶ pick string & fret ─▶ assign notes to strings
                                                        │
                                                        ▼
                                per-string state machine (move → press → bow)
                                                        │
                                                        ▼
        stepper motors  +  finger servos  +  bow-descent servos  +  bow motors
             (RMT engine)      (PCA9685 / GPIO)                     (H-bridge PWM)
```

A controller such as **General-MIDI-Boop** can also ask the instrument, over MIDI
SysEx, *"how many strings do you have, what's your range, which CCs do you
understand?"* and adapt automatically.

---

## Features

- 🎻 **1–4 strings**, each with its own stepper axis, finger, bow motor and bow
  descent servo.
- 🎵 **Automatic note allocation** — send plain MIDI notes and it spreads chords
  across the strings, or **force an exact string/fret** with MIDI CC (tablature).
- 🪕 **Continuous bowing** — a per-string DC motor (a rosined wheel or a linear
  bow) driven through an **H-bridge** (PWM speed + direction + a shared enable).
- 🎚️ **Expressive sustain** — velocity sets bow speed **and** pressure; CC7/CC11
  shape the note *while it plays*.
- 🛰️ **Wi-Fi MIDI** — plays notes received over the network (UDP, port 5006).
- 🖥️ **Local web interface** — setup wizard, live dashboard, MIDI monitor, SysEx
  tester. Runs entirely on the ESP32, no cloud.
- 🧩 **Capability announcement (SysEx)** so a host discovers the instrument.
- 🛡️ **Safety first** — homing before any play, emergency-stop handling, endstop
  monitoring, a shared bow-motor cut, and a fail-safe boot.
- 🔧 **Servos your way** — a PCA9685 board over I²C *or* direct ESP32 pins.

### Instruments it already knows

Ready-made profiles live in [`instrument-profiles/`](instrument-profiles/):
**violin**, **viola**, **cello**, **double bass**. Each is a JSON file you can
tweak or copy from the web wizard.

---

## Quick start

### 1. Try the logic on your PC (no hardware)

The whole musical brain is plain C++ and runs on your laptop:

```bash
cd firmware/test
make            # builds and runs the unit-test suite
```

You should see `152 tests, … checks, 0 failures`.

### 2. Build and flash the firmware

You can use **PlatformIO** or the **Arduino IDE** — same source.

**PlatformIO**

```bash
cd firmware
./sync_web_data.sh          # copy the web UI into the LittleFS image
pio run                     # build for the ESP32-S3-DevKitC-1
pio run -t uploadfs         # upload the web interface
pio run -t upload           # flash the firmware
```

**Arduino IDE** — open `firmware/firmware.ino` (the `src/` folder is compiled
recursively). Full guide: [`docs/ARDUINO_IDE.md`](docs/ARDUINO_IDE.md).

### 3. First configuration

On first boot the ESP32 creates a Wi-Fi access point called
**`Servo-bowed-strings-GMB`**. Connect to it, open the device's address in a
browser, and the **setup wizard** walks you through pins, strings, servos and the
bow motors. See [`docs/FIRST_CONFIGURATION.md`](docs/FIRST_CONFIGURATION.md).

---

## Repository layout

```text
Servo-bowed-strings-GMB/
├── firmware/            ESP32-S3 firmware
│   ├── src/core/        Portable C++ logic (MIDI, allocation, motion, safety) — unit-tested
│   ├── src/platform/    ESP32 adapters (Wi-Fi, web server, servo/bow/stepper drivers, storage)
│   ├── src/main.cpp     Hardware integration / entry point
│   └── test/            Native test suite (runs with g++)
├── web-interface/       Local web app (wizard, dashboard, MIDI monitor, SysEx tester)
├── instrument-profiles/ Example instruments (violin, viola, cello, double bass)
├── board-profiles/      Board pin maps (ESP32-S3-DevKitC-1)
├── hardware/            Reference electronics, wiring, bill of materials
├── mechanics/           Per-string mechanical design
└── docs/                Guides and reference (see below)
```

**Software design in one line:** a pure C++17 core (`firmware/src/core/`, no
Arduino dependency, tested on a PC) plus thin ESP32 adapters
(`firmware/src/platform/esp32/`) — including a `BowBank` H-bridge driver. Details
in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## Documentation

| Guide | What's inside |
| ----- | ------------- |
| [Architecture](docs/ARCHITECTURE.md) | How the code is structured |
| [First configuration](docs/FIRST_CONFIGURATION.md) | Setup wizard walkthrough |
| [Web interface](docs/WEB_INTERFACE.md) | Every page of the local UI |
| [MIDI protocol](docs/MIDI_PROTOCOL.md) | Notes, velocity→bow, CC modulation, CC string/fret selection, SysEx |
| [Pin configuration](docs/PIN_CONFIGURATION.md) | GPIO assignment (incl. the bow H-bridge) & validation |
| [Calibration](docs/CALIBRATION.md) | Fret positions, homing, bow speed & pressure |
| [Safety](docs/SAFETY.md) | Homing, E-stop, bow-motor cut, fault handling |
| [Arduino IDE](docs/ARDUINO_IDE.md) | Building without PlatformIO |

The original specifications are the three markdown files at the repository root:
the full requirements ([`SPECIFICATION.md`](SPECIFICATION.md)), the string/fret
CC selection spec ([`STRING_FRET_SELECTION.md`](STRING_FRET_SELECTION.md)), and
the SysEx capability protocol ([`SYSEX_CAPABILITIES.md`](SYSEX_CAPABILITIES.md)).

---

## Project status

**What is done and verified in CI:**

- Complete, unit-tested logic core (152 native tests, 0 failures).
- Real ESP32-S3 firmware build (PlatformIO) and a fast host compile-check.
- Every shipped instrument profile is loaded through the real firmware parser.
- Web interface (vanilla JS, no build step) and JSON profiles validated.

**Not yet done — hardware validation.** The firmware has **not** been run against
a physical instrument. Bow-motor PWM on a scope, four simultaneous axes, bow
speed/pressure tuning, MIDI endurance, and faulty/missing/inverted sensor
behaviour still need a real test bench. Treat the current state as **ready for
bench bring-up**, not for an unattended, fully-strung instrument under power.

### Safety note

The software emergency-stop is a convenience, **not** a substitute for a hardware
cut of the driver `ENABLE`, the bow H-bridge `ENABLE`, and the motor power. Wire a
physical E-stop before putting motors under load. See
[`docs/SAFETY.md`](docs/SAFETY.md).
