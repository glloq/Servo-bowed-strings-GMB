#include "TestFramework.h"
#include "../src/core/configuration/Profile.h"
#include "../src/core/configuration/ProfileValidator.h"
#include "../src/core/configuration/ServoStroke.h"
#include "../src/core/motion/StepperAxis.h"

using namespace gmb;

// Four-string violin (G3 D4 A4 E5). makeDefault wires a finger + a bowPress servo
// and a bow motor per string, and auto-assigns the pins (incl. the H-bridge).
static Profile violin() {
    return Profile::makeDefault("Violin", 4, {55, 62, 69, 76}, 12);
}

// A two-string instrument leaves plenty of free GPIOs / LEDC channels for the
// tests that add an extra direct-GPIO servo.
static Profile duo() {
    return Profile::makeDefault("Duo", 2, {55, 62}, 12);
}

// Default servos declare a source, a string and a bowed-instrument role.
TEST(default_servos_have_source_and_string) {
    Profile p = violin();
    CHECK(!p.servos.empty());
    for (const auto& s : p.servos) {
        CHECK(s.source == ServoSource::Pca);
        CHECK(s.stringIndex >= 0);
        CHECK(s.function == "finger" || s.function == "bowPress");
    }
    CHECK(ProfileValidator::isActivatable(p));
}

// Every default string gets exactly one bow motor.
TEST(default_profile_has_one_bow_per_string) {
    Profile p = violin();
    CHECK_EQ((int)p.bows.size(), 4);
    for (int i = 0; i < 4; ++i) {
        int found = 0;
        for (const auto& b : p.bows)
            if (b.enabled && b.stringIndex == i) ++found;
        CHECK_EQ(found, 1);
    }
    CHECK(ProfileValidator::isActivatable(p));
}

// A direct-GPIO aux servo on a free pin is valid (works without any PCA).
TEST(direct_gpio_servo_is_valid) {
    Profile p = duo();
    ServoConfig aux;
    aux.enabled = true;
    aux.function = "aux";
    aux.source = ServoSource::DirectGpio;
    aux.gpio = 6;  // free recommended pin on a two-string DevKitC-1 layout
    p.servos.push_back(aux);
    CHECK(ProfileValidator::isActivatable(p));
}

// A direct servo on a reserved pin is rejected.
TEST(direct_servo_on_reserved_pin_rejected) {
    Profile p = duo();
    ServoConfig s;
    s.enabled = true;
    s.function = "aux";
    s.source = ServoSource::DirectGpio;
    s.gpio = 19;  // USB pin
    p.servos.push_back(s);
    CHECK(!ProfileValidator::isActivatable(p));
}

// A direct servo clashing with a stepper STEP pin is rejected.
TEST(direct_servo_conflicts_with_stepper_pin) {
    Profile p = duo();
    int8_t stepGpio = -1;
    for (const auto& a : p.pins)
        if (a.signal == "STEP1") stepGpio = a.gpio;
    CHECK(stepGpio >= 0);
    ServoConfig s;
    s.enabled = true;
    s.function = "aux";
    s.source = ServoSource::DirectGpio;
    s.gpio = stepGpio;
    p.servos.push_back(s);
    CHECK(!ProfileValidator::isActivatable(p));
}

// A direct servo clashing with a bow PWM pin is rejected.
TEST(direct_servo_conflicts_with_bow_pin) {
    Profile p = duo();
    int8_t bowGpio = -1;
    for (const auto& a : p.pins)
        if (a.signal == "BOW_PWM1") bowGpio = a.gpio;
    CHECK(bowGpio >= 0);
    ServoConfig s;
    s.enabled = true;
    s.function = "aux";
    s.source = ServoSource::DirectGpio;
    s.gpio = bowGpio;
    p.servos.push_back(s);
    CHECK(!ProfileValidator::isActivatable(p));
}

// Two servos on the same PCA board+channel conflict.
TEST(duplicate_pca_channel_rejected) {
    Profile p = violin();
    ServoConfig s;
    s.enabled = true;
    s.function = "aux";
    s.stringIndex = -1;
    s.source = ServoSource::Pca;
    s.pcaBoard = 0;
    s.channel = 0;  // already used by finger of string 0
    p.servos.push_back(s);
    CHECK(!ProfileValidator::isActivatable(p));
}

// Up to four PCA boards addressable (0..3); board 4 is rejected.
TEST(pca_board_range) {
    Profile p = violin();
    ServoConfig ok;
    ok.enabled = true; ok.function = "aux"; ok.stringIndex = -1;
    ok.source = ServoSource::Pca; ok.pcaBoard = 3; ok.channel = 5;
    p.servos.push_back(ok);
    CHECK(ProfileValidator::isActivatable(p));

    p.servos.back().pcaBoard = 4;  // out of range
    CHECK(!ProfileValidator::isActivatable(p));
}

// Every enabled string needs its own bow motor to sound.
TEST(string_without_bow_rejected) {
    Profile p = violin();
    for (auto it = p.bows.begin(); it != p.bows.end();) {
        if (it->stringIndex == 0) it = p.bows.erase(it);
        else ++it;
    }
    CHECK(!ProfileValidator::isActivatable(p));
}

// Every enabled string needs a bowPress (bow descent) servo.
TEST(string_without_bowpress_rejected) {
    Profile p = violin();
    for (auto it = p.servos.begin(); it != p.servos.end();) {
        if (it->function == "bowPress" && it->stringIndex == 0) it = p.servos.erase(it);
        else ++it;
    }
    CHECK(!ProfileValidator::isActivatable(p));
}

// A fretted string still needs a finger servo (open-only courses do not).
TEST(fretted_string_without_finger_rejected) {
    Profile p = violin();
    for (auto it = p.servos.begin(); it != p.servos.end();) {
        if (it->function == "finger" && it->stringIndex == 0) it = p.servos.erase(it);
        else ++it;
    }
    CHECK(!ProfileValidator::isActivatable(p));  // maxFret 12 > 0 -> finger required
}

// Two bow motors on one string is a wiring error.
TEST(two_bows_on_one_string_rejected) {
    Profile p = violin();
    BowConfig extra;
    extra.enabled = true;
    extra.stringIndex = 0;  // string 0 already has a bow
    p.bows.push_back(extra);
    CHECK(!ProfileValidator::isActivatable(p));
}

// A bow with minDutyPct > maxDutyPct is rejected.
TEST(bow_inverted_duty_rejected) {
    Profile p = violin();
    p.bows[0].minDutyPct = 90;
    p.bows[0].maxDutyPct = 50;
    CHECK(!ProfileValidator::isActivatable(p));
}

// A bow PWM frequency outside the safe band is rejected.
TEST(bow_pwm_frequency_out_of_range_rejected) {
    Profile p = violin();
    p.bows[0].pwmFreqHz = 60;  // < 100 Hz
    CHECK(!ProfileValidator::isActivatable(p));
}

// LEDC budget: direct-GPIO servos + bow motors must fit the 8 channels. Four bows
// plus five direct servos = 9 > 8.
TEST(ledc_channel_budget_enforced) {
    Profile p = violin();  // 4 bows already use 4 LEDC channels
    // Five extra direct-GPIO aux servos would need five more channels.
    const int8_t freeGpios[5] = {2, 10, 11, 38, 39};  // recommended, distinct
    for (int i = 0; i < 5; ++i) {
        ServoConfig s;
        s.enabled = true; s.function = "aux"; s.stringIndex = -1;
        s.source = ServoSource::DirectGpio; s.gpio = freeGpios[i];
        p.servos.push_back(s);
    }
    CHECK(!ProfileValidator::isActivatable(p));  // 4 + 5 = 9 > 8 LEDC channels
}

// An absurd absolute pulse width (outside the safe servo window) is rejected.
TEST(servo_pulse_absolute_range_rejected) {
    Profile p = violin();
    p.servos[0].pulseMaxUs = 4000;  // > 3000 µs absolute ceiling
    CHECK(!ProfileValidator::isActivatable(p));
}

// A profile with no enabled string can never arm and is rejected.
TEST(zero_enabled_strings_rejected) {
    Profile p = violin();
    for (auto& s : p.strings) s.enabled = false;
    CHECK(!ProfileValidator::isActivatable(p));
}

// When the selector is disabled its CC numbers are unused, so colliding
// string/fret CCs must NOT fail the profile.
TEST(disabled_selector_ignores_cc_collision) {
    Profile p = violin();
    p.selector.enabled = false;
    p.selector.string.ccNumber = 20;
    p.selector.fret.ccNumber = 20;  // identical, but selection is off
    CHECK(ProfileValidator::isActivatable(p));
}

// --- Bow-pressure shaping (servoForceTargetUs) ----------------------------

static ServoConfig bowPressServo() {
    ServoConfig s;
    s.function = "bowPress";
    s.pulseMinUs = 500;
    s.pulseMaxUs = 2500;
    s.restUs = 1000;      // lifted
    s.activeUs = 1800;    // full contact force
    return s;
}

// Velocity scales the bow contact force linearly between rest and active.
TEST(bow_force_follows_velocity) {
    ServoConfig s = bowPressServo();
    CHECK_EQ((int)servoForceTargetUs(s, 0.0), 1000);   // lifted / no force
    CHECK_EQ((int)servoForceTargetUs(s, 1.0), 1800);   // full force
    CHECK_EQ((int)servoForceTargetUs(s, 0.5), 1400);   // midpoint
}

// minForceUs guarantees a floor force so soft notes still speak.
TEST(min_bow_force_floor) {
    ServoConfig s = bowPressServo();
    s.minForceUs = 1300;
    CHECK_EQ((int)servoForceTargetUs(s, 0.0), 1300);   // floored up
    CHECK_EQ((int)servoForceTargetUs(s, 1.0), 1800);   // full still reaches active
}

// The force target is clamped to the servo's mechanical pulse window.
TEST(bow_force_clamped_to_pulse_window) {
    ServoConfig s = bowPressServo();
    s.activeUs = 2500;  // == pulseMax
    CHECK_EQ((int)servoForceTargetUs(s, 1.0), 2500);
    CHECK_EQ((int)servoForceTargetUs(s, 2.0), 2500);   // over-driven intensity clamps
}

// An out-of-window minForceUs is rejected by validation.
TEST(bow_min_force_out_of_range_rejected) {
    Profile p = violin();
    for (auto& s : p.servos)
        if (s.function == "bowPress" && s.stringIndex == 0) s.minForceUs = 3000;  // > pulseMaxUs
    CHECK(!ProfileValidator::isActivatable(p));
}

// Adjustable per-fret positions: the calibrated table overrides theory and is
// what the web fret editor writes.
TEST(adjustable_fret_positions) {
    AxisConfig cfg;
    cfg.scaleLengthMm = 330.0;
    cfg.maxFret = 3;
    // User nudges fret 1 to a measured value.
    cfg.calibratedFretMm = {0.0, 19.5, gmb::fretPositionMm(330.0, 2),
                            gmb::fretPositionMm(330.0, 3)};
    StepperAxis axis(cfg);
    CHECK_NEAR(axis.fretPositionMm(1), 19.5, 1e-9);           // manual override
    CHECK_NEAR(axis.fretPositionMm(2), gmb::fretPositionMm(330.0, 2), 1e-9);
}

// The per-string fret offset (nut position from the FDC) shifts every fret; the
// theoretical spacing is measured from the nut.
TEST(fret_offset_shifts_all_frets) {
    AxisConfig cfg;
    cfg.scaleLengthMm = 330.0;
    cfg.maxFret = 3;
    cfg.fretOffsetMm = 25.0;
    StepperAxis axis(cfg);
    CHECK_NEAR(axis.fretPositionMm(0), 25.0, 1e-9);                                 // nut at the offset
    CHECK_NEAR(axis.fretPositionMm(1), 25.0 + gmb::fretPositionMm(330.0, 1), 1e-9); // + spacing
}

// The offset applies on top of a nut-relative calibrated table too.
TEST(fret_offset_applies_to_calibrated) {
    AxisConfig cfg;
    cfg.scaleLengthMm = 330.0;
    cfg.maxFret = 2;
    cfg.fretOffsetMm = 10.0;
    cfg.calibratedFretMm = {0.0, 19.5, 37.0};  // nut-relative
    StepperAxis axis(cfg);
    CHECK_NEAR(axis.fretPositionMm(0), 10.0, 1e-9);
    CHECK_NEAR(axis.fretPositionMm(1), 10.0 + 19.5, 1e-9);
}

// The travel-fit validator must fold in fretOffsetMm (absolute target), else a
// too-large offset would be silently clamped at play time (audit P1-8 regression).
TEST(fret_offset_beyond_travel_rejected) {
    Profile p = violin();  // scale 330, maxFret 12 -> lastFret 165; maxPositionMm 400
    p.strings[0].fretOffsetMm = 300.0;  // 300 + 165 = 465 > 400
    CHECK(!ProfileValidator::isActivatable(p));
}
TEST(fret_offset_within_travel_valid) {
    Profile p = violin();
    p.strings[0].fretOffsetMm = 20.0;   // 20 + 165 = 185 < 400
    CHECK(ProfileValidator::isActivatable(p));
}
TEST(negative_fret_offset_before_travel_rejected) {
    Profile p = violin();
    p.strings[0].fretOffsetMm = -10.0;  // fret 0 target below minPositionMm (0)
    CHECK(!ProfileValidator::isActivatable(p));
}
// A calibrated value is nut-relative, so the range check must add the offset.
TEST(calibrated_plus_offset_out_of_travel_rejected) {
    Profile p = violin();
    p.strings[0].fretOffsetMm = 350.0;
    p.strings[0].calibratedFretMm = {0.0, 60.0};  // absolute: 350, 410 > 400
    CHECK(!ProfileValidator::isActivatable(p));
}
