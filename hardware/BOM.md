# Bill of materials

Reference bill of materials for **Servo-bowed-strings-GMB**
(SPECIFICATION.md §26). Quantities scale with the string count *N* (1–4). This is the
prototype/reference build with **pluggable driver modules** (§7.2); the
integrated PCB variant is a Phase 5 deliverable (see `hardware/pcb/`).

Part numbers are indicative references, not a mandated sourcing list.

## Electronics — core

| Ref | Qty | Item | Notes |
| --- | :-: | ---- | ----- |
| U1 | 1 | ESP32-S3-DevKitC-1 | main controller (§7.1); verify Flash/PSRAM variant vs GPIO33–37 |
| U2 | 1 | PCA9685 16-ch PWM/servo driver breakout | I²C servo expander (§7.3) |
| U3 | *N* | TMC2209 stepper driver module | pluggable STEP/DIR driver, one per string (§7.2) |
| U4 | *N* | DC-motor H-bridge (e.g. DRV8871 / TB6612) | one bow-motor channel per string: PWM + DIR, shared ENABLE (§7.2) |
| — | 1 | Driver carrier / socket header set | pluggable module sockets |

## Actuators & motors

| Ref | Qty | Item | Notes |
| --- | :-: | ---- | ----- |
| M1..M*N* | *N* | Stepper motor (NEMA, 1.8°, 200 steps/rev) | one per string; size to axis load |
| MB1..MB*N* | *N* | DC bow motor (rosin-coated wheel or linear bow) | one per string; driven by its H-bridge |
| SV_F | *N* | Servo — finger press | PCA9685 channels 0..N−1 |
| SV_B | *N* | Servo — bow descent / contact force (`bowPress`) | mandatory, one per string; PCA9685 channels 4..4+N−1 |
| SV_A | 0–8 | Servo — auxiliary | PCA9685 channels 8–15 |

## Sensors

| Ref | Qty | Item | Notes |
| --- | :-: | ---- | ----- |
| S_H | *N* | HOME reference sensor (optical / Hall / microswitch) | one per axis (§13) |
| S_L | 0–*N* | LIMIT opposite end-stop | optional |

## Mechanics — per string (see `mechanics/README.md`)

| Ref | Qty (per string) | Item | Notes |
| --- | :-: | ---- | ----- |
| — | 1 | Linear guide (rail + carriage) | longitudinal finger travel |
| — | 1 | Transmission set | GT2 belt + 20T pulley + idler, **or** lead screw + nut, **or** rack, **or** cable |
| — | 1 | Carriage / finger assembly | single movable finger |
| — | 1 | Finger-press mechanism | servo-actuated |
| — | 1 | Bow assembly | rosin-coated wheel or linear bow on the DC motor |
| — | 1 | Bow-press descent mechanism | servo-actuated; sets the bow contact force |
| — | 1 | HOME sensor mount + flag | reference datum |

## Power

| Ref | Qty | Item | Notes |
| --- | :-: | ---- | ----- |
| PS1 | 1 | 24 V PSU | stepper motor rail (§22) |
| PS_B | 1 | Bow-motor PSU (per motor voltage) | **separate** bow-motor rail feeding the H-bridges — never the ESP regulator |
| PS2 | 1 | 5–7.4 V PSU / BEC | **separate** servo rail — never the ESP regulator |
| PS3 | 1 | 5 V buck converter | logic rail |
| — | 1 | 3.3 V regulator | on the ESP32-S3 board |

## Protection & passives

| Ref | Qty | Item | Notes |
| --- | :-: | ---- | ----- |
| F1 | 1 | Fuse — stepper motor rail | rate to motor current (§22) |
| F2 | 1 | Fuse — servo rail | rate to combined servo current |
| F3 | 1 | Fuse — bow-motor rail | rate to combined bow-motor stall current |
| D1 | 1 | Reverse-polarity protection (diode / P-MOS) | on incoming supply |
| TVS1 | 1 | TVS diode — motor rail(s) | transient clamp |
| C_drv | 2×*N* | Decoupling caps near each stepper driver | electrolytic + ceramic |
| C_bow | 2×*N* | Decoupling caps near each bow H-bridge | electrolytic + ceramic (motor noise) |
| C_pca | 1 | Bulk reservoir cap ≥ 470 µF near PCA9685 `V+` | servo inrush |
| R_i2c | 2 | I²C pull-ups (2.2–4.7 kΩ to 3.3 V) | if not on the PCA9685 breakout |
| R_sns | 0–*N* | Sensor pull resistors | if internal pulls not used |

## Interconnect

| Ref | Qty | Item | Notes |
| --- | :-: | ---- | ----- |
| J_mot | *N* | Lockable stepper-motor connector (4-pin) | per axis |
| J_bow | *N* | Lockable bow-motor connector (2-pin) | per axis, from the H-bridge |
| J_sv | up to 16 | Servo 3-pin headers | on/from the PCA9685 |
| J_sns | *N* (+LIMIT) | Sensor connector (3-pin) | HOME / LIMIT |
| J_pwr | 4 | Lockable power connectors | 24 V / bow-motor / servo / 5 V |
| — | 1 | E-stop switch | forces drivers off + PCA9685 `/OE` high + `BOW_EN` cut (§21.2) |
| — | as needed | Wire, ferrules, GT2 belt/pulley or lead screw, fasteners | mechanics |

## Notes

* **Servo count** = finger (*N*, mandatory) + bowPress (*N*, mandatory) + aux
  (0–8), total ≤ 16 across the PCA9685 (§6). Each string therefore needs two
  servos (finger + bowPress).
* **One bow motor per string** through its own H-bridge; a single shared
  `BOW_EN` disables every bridge at once and is wired into the hardware safety
  cut (§21.2).
* **LEDC budget**: the four `BOW_PWM` speed outputs use the ESP32-S3's LEDC
  peripheral, which it shares with any direct-GPIO servos — keep
  `direct-GPIO servos + bow PWMs ≤ 8` (steppers use RMT, PCA9685 servos are
  off-budget).
* **Driver current** must be set per TMC2209 to the motor rating before use.
* Confirm the ESP32-S3 module variant: octal-PSRAM parts consume GPIO35–37 (kept
  reserved in the board profile); GPIO33 (default shared `BOW_EN`) is a caution
  pin, usable after that check.
