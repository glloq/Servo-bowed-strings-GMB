# Wiring guide

Connection guide for the **Servo-bowed-strings-GMB** reference electronics
(SPECIFICATION.md §7 and §22). Default GPIO come from the ESP32-S3-DevKitC-1
board profile (§11.5, `board-profiles/esp32-s3-devkitc-1.json`); every line can
be reassigned from the web interface.

> ⚠️ Wire and power-check the machine unpowered, with drivers **disabled** and
> the PCA9685 `/OE` **high** (servos off). The firmware boots into a safe state:
> drivers off, servos neutralised, aux outputs cut (§21.1).

## 1. Default GPIO map (ESP32-S3-DevKitC-1)

| Function | Strings 1 → 4 (GPIO) |
| -------- | -------------------- |
| STEP | 4, 5, 6, 7 |
| DIR | 17, 18, 8, 9 |
| HOME | 12, 13, 14, 21 |
| BOW_PWM (bow-motor speed, LEDC) | 1, 2, 10, 11 |
| BOW_DIR (bowing direction) | 15, 16, 38, 39 |

| Single signal | GPIO |
| ------------- | :--: |
| I²C SDA | 40 |
| I²C SCL | 41 |
| Global driver ENABLE | 42 |
| PCA9685 `/OE` (servo safety) | 47 |
| Shared bow H-bridge `BOW_EN` | 33 |

Reserved / do-not-use on this board: GPIO0/3/45/46 (strapping), 19/20 (future
USB), 26–32 (Flash/PSRAM), 35/36/37 (variant memory), 34 (caution), 43/44
(UART0 programming/diagnostics), 48 (RGB LED). GPIO33 is a caution pin used here
for the shared `BOW_EN` (verify it is free on your module variant). GPIO22–25 do
not exist on the ESP32-S3.

## 2. Stepper drivers (TMC2209, one per string)

Each pluggable driver module connects as follows:

| Driver pin | Connect to | Notes |
| ---------- | ---------- | ----- |
| `STEP` | ESP32-S3 STEP GPIO (per string) | fast digital output |
| `DIR` | ESP32-S3 DIR GPIO (per string) | direction |
| `EN` (`/ENABLE`) | Global ENABLE (GPIO42) | active-low; one line drives all drivers |
| `VM` / `GND` | 24 V motor rail / common ground | see Power |
| `VIO` | 3.3 V logic | driver logic reference |
| `A1 A2 B1 B2` | stepper motor coils | check motor datasheet pairing |
| `DIAG` (optional) | spare input GPIO | TMC2209 stall/diag |
| `UART` (optional) | spare UART | TMC2209 configuration |
| `MS1 / MS2` | set per module | microstep select (match `microsteps` in the profile) |

HOME sensor for each axis:

| Sensor pin | Connect to |
| ---------- | ---------- |
| Signal | ESP32-S3 HOME GPIO (per string) — interrupt-capable |
| VCC | 3.3 V |
| GND | common ground |

Use the internal pull-up/down where the sensor allows it, or an external
resistor otherwise. The firmware `sensorActiveHigh` field selects the active
level; NO and NC contacts are both supported (§5 wizard, §13). Optional LIMIT
end-stops wire the same way on spare interrupt-capable GPIO.

**Set the driver motor current** on each TMC2209 (VREF / UART) before enabling.

## 3. Bow motors (H-bridge, one per string)

Each string is bowed by its own DC motor through an H-bridge (e.g. DRV8871 /
TB6612). Three logic lines per bridge, plus one line shared by all bridges:

| H-bridge pin | Connect to | Notes |
| ------------ | ---------- | ----- |
| `PWM` / `IN` (speed) | ESP32-S3 `BOW_PWM<n>` GPIO (per string) | LEDC PWM, ~20 kHz (ultrasonic) — sets motor speed |
| `DIR` / `PH` (direction) | ESP32-S3 `BOW_DIR<n>` GPIO (per string) | bowing direction; alternates for down-bow/up-bow |
| `EN` / `STBY` | Shared `BOW_EN` (GPIO33) | **one line disables every bridge at once** |
| `VM` / `GND` | Bow-motor rail / common ground | **separate** motor rail — see Power |
| `OUT1 / OUT2` | bow DC motor terminals | one motor per bridge |

### `BOW_EN` safety behaviour

`BOW_EN` is the single hardware enable for **all** bow bridges. Firmware holds it
**disabled** at boot and during homing, panic and E-stop, so no bow can spin; it
is asserted only when the instrument is armed (`Ready`). Wire `BOW_EN` into the
**same hardware safety cut** as the driver `ENABLE` and the PCA9685 `/OE` so one
E-stop kills the steppers, the servos and the bow motors together (§21).

Keep the bow-motor rail and its return **separate** from logic; add per-bridge
decoupling (electrolytic + ceramic) close to each H-bridge to tame motor noise.

## 4. PCA9685 servo expander (I²C)

| PCA9685 pin | Connect to | Notes |
| ----------- | ---------- | ----- |
| `SDA` | GPIO40 | I²C data |
| `SCL` | GPIO41 | I²C clock |
| `VCC` | 3.3 V | chip logic |
| `V+` | 5–7.4 V servo rail | **separate** servo supply, not the ESP regulator |
| `GND` | common ground | shared with logic and servo supply |
| `/OE` | GPIO47 | **safety**: drive high to disable all outputs |

Pull-ups on SDA/SCL (2.2 kΩ–4.7 kΩ to 3.3 V) — many PCA9685 breakouts include
them. A **bulk reservoir capacitor** (≥ 470 µF) sits across `V+`/`GND` next to
the board (§22).

### `/OE` safety behaviour

`/OE` is active-low output-enable. Firmware holds it **high (outputs off)** at
boot and during panic/E-stop so no servo can move; it is pulled low only when the
configuration is validated and servos are armed (§21.1–21.3). A hardware E-stop
may also force `/OE` high directly.

### Servo channel map (§7.3)

| Channel | Function | Servo config `function` |
| :-----: | -------- | ----------------------- |
| 0–3 | finger press (one per string) | `finger` |
| 4–7 | bow descent / contact force (one per string) | `bowPress` |
| 8–15 | auxiliary | `aux` |

In the example profiles: finger servos on channels `0 … N−1`, bow-press servos on
`4 … 4+N−1`.

## 5. Power rails (§22)

| Rail | Feeds | Source |
| ---- | ----- | ------ |
| **24 V** | stepper motors (through the drivers) | dedicated PSU |
| **bow-motor rail** | bow DC motors (through the H-bridges) | **separate** bow-motor PSU |
| **5–7.4 V** | servomotors (PCA9685 `V+`) | **separate** servo PSU/BEC |
| **5 V** | logic | buck from 24 V or its own supply |
| **3.3 V** | ESP32-S3, driver `VIO`, sensors | ESP board regulator |

Mandatory measures:

* **Separate servo and bow-motor supplies** — no servo or bow motor is ever
  powered from the ESP32 regulator.
* **Fuse the stepper rail, the bow-motor rail and the servo rail** independently.
* **Reverse-polarity protection** on the incoming supply.
* **TVS diode** across the motor rail(s) (transient clamp).
* **Decoupling capacitors** close to each driver module and each bow H-bridge
  (electrolytic + ceramic).
* **Bulk reservoir capacitor** near the PCA9685 `V+`.
* **Structured common ground** — star/plane ground tying all rails at one point.
* **Lockable connectors** on stepper-motor, bow-motor, servo and power harnesses.
* **`BOW_EN`, driver `ENABLE` and PCA9685 `/OE` all wired into the hardware
  safety cut** (§21.2).

## 6. Grounding & signal integrity

* Single, structured common ground reference for the 24 V return, the bow-motor
  return, 5 V, 3.3 V and signal grounds.
* Keep STEP/DIR runs short; twist motor phase pairs; twist each bow-motor pair;
  route them away from the HOME sensor and I²C wiring.
* Keep I²C (SDA/SCL) short or add stronger pull-ups; a bulk cap stabilises the
  servo rail against inrush when several servos move together.

## 7. Bring-up checklist

1. Wire everything with all supplies **off**.
2. Continuity-check grounds and confirm no rail-to-rail shorts.
3. Power **logic/3.3 V only**; confirm the ESP32-S3 boots and serves the web UI.
4. Power the **servo rail**; with `/OE` high, verify no servo twitches, then arm
   and test one finger servo and its bow-press servo.
5. Set each **driver current**, power the **24 V** rail, and home one axis at low
   speed before enabling the rest.
6. With `BOW_EN` still cut, power the **bow-motor rail**; confirm no motor spins,
   then arm and test one bow at low duty.
7. Verify the **STOP / panic** path disables drivers, forces `/OE` high **and
   cuts `BOW_EN`** (every bow motor stops).
