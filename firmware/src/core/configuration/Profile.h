// Instrument configuration profile (spec section 20).
//
// This is the single source of truth for the firmware. The web UI edits a draft
// which is validated and then atomically activated; SysEx capabilities and the
// runtime are rebuilt from the active profile only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../board/PinManager.h"
#include "../instrument/NoteAllocator.h"
#include "../midi/StringFretSelector.h"
#include "../motion/HomingController.h"
#include "../motion/StepperAxis.h"

namespace gmb {

enum class NetworkMode : uint8_t { AccessPoint = 0, Station = 1 };

enum class VelocityCurve : uint8_t { Linear, Soft, Hard, Exponential, Custom };

struct InstrumentInfo {
    std::string name = "Instrument";
    std::string description;
    uint8_t stringCount = 4;
    std::string type = "violin";
    uint8_t gmProgram = 40;   // GM 40 = violin
    uint8_t typeId = 0x05;    // GMB instrument type id (bowed string)
    int8_t capo = 0;
    int8_t transpose = 0;
};

struct NetworkConfig {
    NetworkMode mode = NetworkMode::AccessPoint;
    std::string ssid;            // station SSID (never exported with password)
    std::string hostname = "gmb-instrument";
    std::string apSsid = "Servo-bowed-strings-GMB";
    bool staticIp = false;
};

struct MidiConfig {
    uint8_t globalChannel = 0;   // zero-based internal channel
    bool omni = false;
    int8_t transpose = 0;
    uint8_t chordWindowMs = 3;   // grouping window (spec 17.2)
    VelocityCurve velocityCurve = VelocityCurve::Linear;
    bool sustainPedal = true;
    uint8_t sustainCc = 64;
    SaturationStrategy saturationStrategy = SaturationStrategy::PriorityLow;

    // Playback timing / latency management.
    //   noteExecutionDelayMs : fixed delay between receiving a Note On and the
    //                          note actually sounding, so the mechanics have a
    //                          predictable, constant window to get in position.
    //   fingerLeadMs         : begin the finger descent up to this long before
    //                          the carriage is estimated to reach the fret, so
    //                          the finger arrives on the string around the same
    //                          time (overlaps descent with the approach).
    //   bowLeadMs            : begin lowering the bow (descent servo) up to this
    //                          long before the string is ready, so the bow wheel
    //                          is already in contact when the note is due — the
    //                          motor spin-up (BowConfig.spinUpMs) then overlaps
    //                          the last of the approach.
    // The two leads shrink the minimum achievable noteExecutionDelayMs; both
    // default to 0 (no anticipation — the safe, strictly-sequential behaviour).
    uint16_t noteExecutionDelayMs = 0;
    uint16_t fingerLeadMs = 0;
    uint16_t bowLeadMs = 0;
};

// Where a servo's PWM signal comes from. The system must work with OR without a
// PCA9685 (a servo can hang directly off a free ESP32 GPIO), and both can be
// mixed on the same instrument.
enum class ServoSource : uint8_t { Pca = 0, DirectGpio = 1 };

// Servo roles. Per-string roles carry a stringIndex; shared roles use -1.
//   finger   : presses the string at the fret to select the pitch (per string)
//   bowPress : the bow descent servo — lowers the bow wheel/motor onto the
//              string and sets the CONTACT FORCE (per string, mandatory). Its
//              active pulse is the full-force position; velocity scales the
//              pressure between rest (lifted) and active.
//   aux      : any auxiliary actuator
// (The string is set in vibration by a DC bow motor — see BowConfig — not by a
// servo, so there are no pluck / strum / damper roles on a bowed instrument.)
// (Function is kept as a string so the web UI can offer new roles without a
// firmware change.)
struct ServoConfig {
    bool enabled = false;
    std::string function = "finger";
    int8_t stringIndex = -1;      // owning string, or -1 for a shared/global servo

    // Signal source.
    ServoSource source = ServoSource::Pca;
    uint8_t pcaBoard = 0;         // 0..3 : up to four PCA9685 (0x40..0x43)
    uint8_t channel = 0;          // PCA9685 channel 0..15 (source == Pca)
    int8_t gpio = -1;             // ESP32 GPIO           (source == DirectGpio)

    // Motion calibration (microseconds).
    uint16_t pulseMinUs = 500;
    uint16_t pulseMaxUs = 2500;
    uint16_t restUs = 1000;       // finger up / bow lifted off the string
    uint16_t activeUs = 1800;     // finger down / bow at full contact force
    bool inverted = false;
    uint16_t travelMs = 120;
    uint16_t settleMs = 30;
    bool disableAtRest = true;

    // Continuous-contact shaping (bow pressure). The bowPress servo does not
    // strike-and-return like a plectrum: it descends and HOLDS a force between
    // rest and active, scaled by velocity/expression (servoForceTargetUs).
    //   minForceUs : guaranteed minimum pulse toward the active side so a
    //                low-velocity note still makes contact and speaks
    //                (0 = disabled, the force follows velocity only).
    uint16_t minForceUs = 0;
};

// Per-string bow drive: a DC motor spinning a rosined wheel / linear bow against
// the string, driven through an H-bridge (PWM speed + DIR direction + a single
// shared ENABLE). The string sounds continuously while the motor turns; velocity
// and expression set the speed here and the contact force on the bowPress servo.
// The PWM / DIR GPIOs live in Profile::pins as BOW_PWM<i> / BOW_DIR<i>, and the
// shared enable as BOW_EN — exactly like the stepper STEP/DIR/ENABLE lines.
struct BowConfig {
    bool enabled = false;
    int8_t stringIndex = -1;

    // Speed calibration, as an H-bridge PWM duty cycle (percent).
    uint8_t minDutyPct = 25;    // lowest duty that reliably keeps the string bowing
    uint8_t maxDutyPct = 100;   // duty at full velocity
    uint16_t pwmFreqHz = 20000; // ultrasonic so the switching is inaudible

    // Spin ramps (ms): a motor cannot start/stop instantly, and ramping keeps the
    // attack/release smooth instead of a click.
    uint16_t spinUpMs = 40;     // rest -> target when a note starts
    uint16_t spinDownMs = 60;   // target -> rest at note off

    bool reverse = false;       // invert the default DIR level (bowing direction)
    bool alternate = false;     // alternate DIR each note (down-bow / up-bow)
};

struct Profile {
    std::string project = "Servo-bowed-strings-GMB";
    uint16_t profileVersion = 1;
    uint32_t capabilitiesRevision = 1;

    InstrumentInfo instrument;
    std::string boardIdentifier = "esp32-s3-devkitc-1";
    bool reserveUsb = true;
    bool automaticPinAssignment = true;
    std::vector<PinAssignment> pins;

    NetworkConfig network;
    MidiConfig midi;
    SelectorConfig selector;

    std::vector<AxisConfig> strings;
    std::vector<HomingConfig> homing;
    std::vector<ServoConfig> servos;
    std::vector<BowConfig> bows;    // one DC bow motor (H-bridge) per string

    // Build an InstrumentView (used by the selector and capabilities) from the
    // string list.
    InstrumentView instrumentView() const;

    // Convenience: create a sensible default profile for a given instrument.
    static Profile makeDefault(const std::string& name, uint8_t stringCount,
                               const std::vector<uint8_t>& tuning, uint8_t maxFret);
};

}  // namespace gmb
