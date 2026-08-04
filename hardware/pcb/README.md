# PCB — Phase 5 deliverable

The integrated PCB is a **Phase 5 (dedicated hardware)** deliverable of the
SPECIFICATION.md (§24, §26). It is not yet designed; this directory is a
placeholder. The prototype build uses an ESP32-S3-DevKitC-1 with **pluggable
TMC2209 driver modules**, **per-string bow-motor H-bridges** and a PCA9685
breakout (§7.2) rather than a custom board.

See `../schematics/README.md` for the schematic that the PCB will implement, and
`../wiring/WIRING.md` for the current reference interconnect.

## Planned contents

When produced, the PCB package will include:

* Board layout hosting the ESP32-S3 module, **1–4 pluggable TMC2209 sockets**,
  **1–4 bow-motor H-bridges**, and the PCA9685 (on-board or headered).
* **Separated power planes/rails** (§22): 24 V stepper, separate bow-motor rail,
  5–7.4 V servo, 5 V logic, 3.3 V, with a structured common-ground strategy.
* On-board **protection**: stepper-, bow-motor- and servo-rail fuses,
  reverse-polarity protection, TVS on the motor rail(s), per-driver and
  per-H-bridge decoupling, PCA9685 reservoir capacitor.
* **Lockable connectors** for stepper motors, bow motors, servos, sensors and
  power (§22).
* **Hardware E-stop** wiring: driver disable + PCA9685 `/OE` neutralisation +
  shared bow `BOW_EN` cut, ESP32 kept alive (§21.2).
* Manufacturing outputs: Gerbers, drill files, assembly drawing, and a
  pick-and-place / BOM cross-reference to `../BOM.md`.
* Electrical validation notes (§24).
