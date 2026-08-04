// Pure bow-pressure shaping (unit-tested on the host).
//
// Given the bowPress servo's calibration and a normalised intensity (0..1, from
// MIDI velocity / expression), compute the pulse that sets the bow's CONTACT
// FORCE. Unlike a plucked striker this does not return to rest on its own: the
// bow descends and HOLDS this pulse while the note sounds, and the value is
// recomputed live as CC7/CC11 modulate a held note. Honours the guaranteed
// minimum contact force so a soft note still speaks. Kept out of ServoBank (a
// platform class) so the maths is unit-tested natively without the Arduino
// runtime. `inverted` is NOT applied here — the bank mirrors within the pulse
// window at write time.
#pragma once

#include <cstdint>

#include "Profile.h"

namespace gmb {

inline uint16_t servoForceTargetUs(const ServoConfig& s, double intensity) {
    if (intensity < 0.0) intensity = 0.0;
    if (intensity > 1.0) intensity = 1.0;

    // Interpolate between rest (lifted, no force) and active (full contact force).
    int span = static_cast<int>(s.activeUs) - static_cast<int>(s.restUs);
    double target = static_cast<double>(s.restUs) + intensity * span;

    // Guaranteed minimum contact force toward the active side, so a low-velocity
    // note still presses the bow onto the string hard enough to speak.
    if (s.minForceUs != 0) {
        if (span >= 0) {
            if (target < s.minForceUs) target = s.minForceUs;
        } else {
            if (target > s.minForceUs) target = s.minForceUs;
        }
    }

    // Clamp to the servo's mechanical pulse window.
    if (target < s.pulseMinUs) target = s.pulseMinUs;
    if (target > s.pulseMaxUs) target = s.pulseMaxUs;
    return static_cast<uint16_t>(target + 0.5);
}

}  // namespace gmb
