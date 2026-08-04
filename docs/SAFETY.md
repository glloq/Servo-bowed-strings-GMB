# Safety — Servo-bowed-strings-GMB

> Sources: `SPECIFICATION.md` §21, §22 · Code: `core/safety/SafetyManager.{h,cpp}`.
> Related documents: [`ARCHITECTURE.md`](ARCHITECTURE.md) · [`CALIBRATION.md`](CALIBRATION.md) · [`WEB_INTERFACE.md`](WEB_INTERFACE.md).

The `SafetyManager` centralizes safe states, software panic, hardware emergency
stop, and the fault log.

```cpp
enum class SafetyState {
    PowerOnSafe,   // drivers off, servos neutralised, queues empty
    Armed,         // normal operation
    Panic,         // latched software panic
    EmergencyStop, // hardware stop asserted
};
```

`actuatorsAllowed()` only returns `true` in the `Armed` state: no actuator moves
in any other state.

---

## 1. State at startup (§21.1)

At power-on, `boot()` places the system into `PowerOnSafe`:

```text
drivers disabled
servos neutralised
bow motors stopped, shared BOW_EN cut
auxiliary outputs cut
MIDI queues empty
profile verified
GPIO validated
```

The transition to `Armed` is only possible **after** validation of the profile
and the GPIO pins:

```cpp
bool arm(bool profileValid, bool pinsValid);  // Armed only if both are true
```

No actuator is enabled in normal mode as long as critical errors remain
uncorrected (see the wizard §9, [`WEB_INTERFACE.md`](WEB_INTERFACE.md)).

### Complete startup sequence (§13 / §21.1)

`main.cpp` follows a three-phase state machine; **playback is only armed after a
successful homing**, so that no axis moves from an unknown physical position:

```text
Boot     : PowerOnSafe — profile loaded and validated, drivers OFF, servos at rest
   │        (if the profile is invalid, it stays in Boot: no movement)
   ▼
Homing   : drivers ON, bow motors still DISABLED (shared BOW_EN held cut);
   │        each axis runs its HomingController (non-blocking, in parallel). The
   │        origin is anchored on the HOME sensor (0 mm). A faulty axis is
   │        disabled without blocking the others.
   ▼
Ready    : all axes homed → arm() → BOW_EN enabled → MIDI notes are played.
```

During `Boot` and `Homing`, `Note On` messages are not played (only SysEx
requests are processed) and **the bow motors stay disabled** — `BOW_EN` is only
asserted once the instrument is armed (`Ready`), so a bow can never spin against
a string whose position is still unknown. A mechanical configuration change from
the Web interface triggers a new homing (bows disabled again) before playback
resumes.

### Hardware emergency stop and limit switches

* **Hardware E-stop**: if an `ESTOP` pin is assigned (active low), `loop()`
  reads it on every pass and immediately triggers a panic (drivers cut off,
  servos neutralized, **every bow motor stopped and the shared `BOW_EN`
  cut**). Without an assigned `ESTOP` pin, only the software panic
  (Web STOP button / CC120/CC123) is available.
* **`LIMIT` switches**: an active `LIMIT` during a movement causes an
  **immediate stop** of the axis concerned (not a deceleration), invalidates its
  position (re-homing required) and puts it into a fault state, without
  disturbing the other axes.

### Recovery after panic / E-stop (`POST /api/reset`)

After a panic or an E-stop, the safety state is **locked**: neither loading a
profile nor a new homing can re-enable the motors. Recovery is explicit via
`POST /api/reset` (the "Reset & re-home" button on the dashboard), accepted only
if:

* the E-stop is physically released;
* no `LIMIT` is active;
* the profile is valid;
* all axes/servos were able to attach their hardware channel.

Recovery then forces a **new homing** before playback resumes.

### Degraded mode (`readyDegraded`)

If one or more axes fail their homing, the system still enters playback **but**:

* the failing strings are disabled (no note, even under CC selection);
* the **polyphony announced** via SysEx is reduced to the number of functional
  axes and the **capabilities revision** is incremented (General-Midi-Boop stops
  sending the unplayable notes);
* the exposed state becomes `readyDegraded` and the fault appears on the
  dashboard.

### Wi-Fi secrets and access

* Wi-Fi passwords (station and access point) are stored in **NVS**
  (`Preferences`), never in the exportable profile, and are set via
  `POST /api/wifi`. The access point can be protected with WPA2 (password ≥ 8
  characters); otherwise it remains open.
### Web API authentication

The routes that **move the mechanics or change the configuration**
(`PUT /api/profile`, `/api/profiles*`, `/api/reset`, `/api/test/note`,
`/api/test/servo`, `/api/wifi`) are protected by an **administrator token**
stored in NVS:

* as long as no token is defined (first startup), writes are allowed to enable
  the initial configuration;
* once defined via `POST /api/auth`, each write must provide the matching
  `X-GMB-Token` header; otherwise the request is refused (401);
* `POST /api/panic` remains **always** accessible (safety);
* the status exposes `authConfigured` and `apOpen` so the interface can warn
  when the access point is open **and** without a token.

**Recommendation**: on an untrusted network, set an AP password (WPA2, ≥ 8
characters) **and** an administrator token.

**Known remaining limitations**: no dedicated CSRF/origin protection yet, no
local physical confirmation for reset/homing, and no formal separation between
the MIDI network and the administration network — see the limitations note in
the [`README`](../README.md).

---

## 2. Hardware emergency stop — the PCA9685 /OE and the bow ENABLE (§21.2)

A **hardware** stop must be able to:

* disable the stepper drivers (their shared `ENABLE` line);
* disable the PCA9685 via its `/OE` pin (immediate neutralization of all servos,
  independently of the firmware);
* **cut the shared bow `BOW_EN`** so every H-bridge — and therefore every bow
  motor — is disabled at once, independently of the firmware;
* neutralize the auxiliary outputs;
* **keep the ESP32 powered** (for logging and controlled recovery).

The PCA9685 `/OE` output is wired to a safety pin (GPIO47 in the recommended
DevKitC-1 profile — see [`PIN_CONFIGURATION.md`](PIN_CONFIGURATION.md)), and the
bow H-bridge `BOW_EN` (GPIO33) is wired into the **same hardware safety cut**,
alongside the driver `ENABLE` and the PCA9685 `/OE`, so one E-stop kills the
steppers, the servos and the bow motors together. `emergencyStop(nowMs)` locks
the `EmergencyStop` state and records the cause.

---

## 3. Software panic (§21.3)

`panic(cause, nowMs)` locks the `Panic` state, records the cause, and the
firmware must:

* flush the MIDI queue;
* cancel all movements;
* stop every bow motor and cut the shared `BOW_EN`;
* lift the bows and the fingers;
* neutralize the servos;
* disable the stepper motors;
* record the cause.

The `StringController` command-identifier mechanism guarantees that no deferred
bow attack is executed after a panic (see [`ARCHITECTURE.md`](ARCHITECTURE.md)
§3 and `SPECIFICATION.md` §16). `reset()` returns to `PowerOnSafe` and requires
a re-arm.

The Web API exposes `POST /api/panic` ([`WEB_INTERFACE.md`](WEB_INTERFACE.md)).

---

## 4. Wi-Fi loss (§21.4)

Configurable behavior (`WifiLossBehavior`):

| Valeur | Comportement |
| ------ | ------------ |
| `FinishThenStop` (0) | **default**: cancel pending commands, controlled release, return to READY |
| `StopImmediately` (1) | stop immediately |
| `ContinueQueued` (2) | continue commands already queued |
| `IdleKeepMotors` (3) | return to idle without disabling the motors |

Default behavior in detail: cancellation of pending commands, controlled
release, return to the READY state. Releasing a sounding note **lifts its bow
and stops its motor**, so no bow keeps sawing an unattended string; only
`IdleKeepMotors` leaves the stepper drivers energised (the bow motors are stopped
regardless, since no note is sounding in idle).

---

## 5. Fault log

`SafetyManager` incorporates the role of `FaultManager` (§23):

```cpp
struct FaultRecord { std::string source; std::string message; uint32_t atMs; };

void recordFault(source, msg, nowMs);
const std::vector<FaultRecord>& faults() const;
void clearFaults();
```

Faults are displayed on the Web dashboard (§19).

---

## 6. Power supply (§22)

Recommended rails:

| Rail | Usage |
| ---- | ----- |
| 24 V | stepper motors |
| motor rail | bow DC motors (through the H-bridges) |
| 5 to 7.4 V | servomotors |
| 5 V | logic |
| 3.3 V | ESP32-S3 |

The bow DC motors run from a **separate motor rail through their H-bridges**,
exactly like the stepper drivers run from the 24 V rail — never from the ESP32
regulator or the logic rail. Size the rail to the combined stall current of the
bow motors and give it its own fuse.

Requirements:

* **separate** servo power supply;
* stepper-motor fuse; bow-motor fuse; servo fuse;
* reverse-polarity protection;
* TVS on the motor rail(s);
* capacitors near the drivers and near the bow H-bridges; a reserve capacitor
  near the PCA9685;
* structured common ground;
* lockable connectors;
* the bow H-bridge `BOW_EN` wired into the hardware safety cut, alongside the
  driver `ENABLE` and the PCA9685 `/OE` (§21.2);
* **no servo or bow motor powered from the ESP32 regulator**.
