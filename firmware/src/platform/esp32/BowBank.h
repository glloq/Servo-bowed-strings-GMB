// H-bridge bow-motor bank for 1..4 strings (spec §5.3, §7).
//
// Each string is set in vibration by a DC motor spinning a rosined wheel / bow
// against it. The motor is driven through an H-bridge with three lines:
//   BOW_PWM<i> : speed  (LEDC PWM, ultrasonic so the switching is inaudible)
//   BOW_DIR<i> : direction (plain GPIO — bowing sense, alternated per note)
//   BOW_EN     : one shared ENABLE / STBY that neutralises every bridge at once
// The string sounds CONTINUOUSLY while the motor turns; intensity (from velocity
// and CC7/CC11) sets the duty between BowConfig.minDutyPct and maxDutyPct, and a
// spin-up / spin-down ramp keeps the attack and release smooth. The steppers use
// the RMT engine, so the bow PWM outputs are the only LEDC users besides any
// direct-GPIO servos (8 channels total on the ESP32-S3).
#pragma once

#include <cstdint>
#include <vector>

#include "../../core/configuration/Profile.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace gmb {

struct BowPins {
    int8_t pwm = -1;
    int8_t dir = -1;
};

class BowBank {
public:
    // pins[i] carries the PWM/DIR GPIOs for bow i (parallel to `bows`); enablePin
    // is the shared H-bridge ENABLE/STBY (active-high). ledcBase is the first LEDC
    // channel free after the direct-GPIO servos (used only by the 2.x LEDC API).
    void begin(const std::vector<BowConfig>& bows, const std::vector<BowPins>& pins,
               int8_t enablePin, int ledcBase);

    void enable(bool on);            // shared ENABLE/STBY (all H-bridges)
    // Target bowing intensity 0..1 for bow i (0 = stop). Chooses the DIR level
    // (with per-note alternation) on the 0->nonzero transition and ramps the PWM
    // duty toward the target over spinUp (starting/raising) or spinDown (to 0).
    void setIntensity(int i, double intensity, uint32_t nowMs);
    void stop(int i, uint32_t nowMs);  // ramp down to a standstill (spinDownMs)
    void stopAll();                  // immediate hard stop of every motor (safety)
    void update(uint32_t nowMs);     // advance the spin ramps; call from loop()

    bool attachFault() const { return attachFault_; }
    size_t count() const { return bows_.size(); }
    bool enabled() const { return enabled_; }
    int indexForString(int stringIndex) const;
    // Current duty 0..1 of bow i (for the web dashboard / diagnostics).
    double duty(int i) const {
        return (i >= 0 && i < (int)rt_.size()) ? rt_[i].curDuty : 0.0;
    }

private:
    struct Rt {
        double curDuty = 0.0;     // current duty 0..1 (after ramp)
        double startDuty = 0.0;   // duty at ramp start
        double targetDuty = 0.0;  // duty we are ramping toward
        uint32_t rampStartMs = 0;
        uint16_t rampMs = 0;
        bool running = false;     // motor currently commanded to turn (duty > 0)
        bool dirLevel = false;    // current DIR pin level (after reverse/alternate)
        bool parity = false;      // toggles per note for `alternate`
    };
    std::vector<BowConfig> bows_;
    std::vector<BowPins> pins_;
    std::vector<Rt> rt_;
    std::vector<int8_t> ledcCh_;  // LEDC channel per bow (2.x API)
    int8_t enablePin_ = -1;
    bool enabled_ = false;
    bool attachFault_ = false;
    int ledcBase_ = 0;

    void applyDuty(int i);                                 // write the PWM output
    double targetDutyFor(int i, double intensity) const;   // minDuty..maxDuty
};

}  // namespace gmb
