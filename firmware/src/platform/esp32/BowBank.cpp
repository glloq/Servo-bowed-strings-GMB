#include "BowBank.h"

namespace gmb {

namespace {
#if defined(ARDUINO)
// 10-bit duty (0..1023). At 10 bits the LEDC clock supports any bow PWM frequency
// in the validated 100..40000 Hz band (freq * 2^10 stays below the 80 MHz source).
constexpr uint8_t kBowResBits = 10;
constexpr uint32_t kBowMaxDuty = (1u << kBowResBits) - 1;
uint32_t dutyToRaw(double duty01) {
    if (duty01 < 0.0) duty01 = 0.0;
    if (duty01 > 1.0) duty01 = 1.0;
    return static_cast<uint32_t>(duty01 * kBowMaxDuty + 0.5);
}
#endif
}  // namespace

void BowBank::begin(const std::vector<BowConfig>& bows,
                    const std::vector<BowPins>& pins, int8_t enablePin,
                    int ledcBase) {
    bows_ = bows;
    pins_ = pins;
    pins_.resize(bows_.size());  // tolerate a short pin list (missing -> -1)
    rt_.assign(bows_.size(), Rt{});
    ledcCh_.assign(bows_.size(), -1);
    enablePin_ = enablePin;
    ledcBase_ = ledcBase;
    enabled_ = false;
    attachFault_ = false;

#if defined(ARDUINO)
    if (enablePin_ >= 0) {
        pinMode(enablePin_, OUTPUT);
        digitalWrite(enablePin_, LOW);  // ENABLE/STBY low = every bridge disabled
    }
    for (size_t i = 0; i < bows_.size(); ++i) {
        const BowConfig& b = bows_[i];
        if (!b.enabled) continue;
        if (pins_[i].dir >= 0) {
            pinMode(pins_[i].dir, OUTPUT);
            digitalWrite(pins_[i].dir, LOW);
        }
        if (pins_[i].pwm < 0) { attachFault_ = true; continue; }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        if (!ledcAttach(pins_[i].pwm, b.pwmFreqHz, kBowResBits)) {
            attachFault_ = true;
            continue;
        }
        ledcWrite(pins_[i].pwm, 0);
#else
        int ch = ledcBase_ + static_cast<int>(i);
        if (ch >= 8) { attachFault_ = true; continue; }  // out of LEDC channels
        ledcCh_[i] = static_cast<int8_t>(ch);
        ledcSetup(ch, b.pwmFreqHz, kBowResBits);
        ledcAttachPin(pins_[i].pwm, ch);
        ledcWrite(ch, 0);
#endif
    }
#else
    (void)ledcBase;
#endif
}

double BowBank::targetDutyFor(int i, double intensity) const {
    if (i < 0 || i >= (int)bows_.size()) return 0.0;
    const BowConfig& b = bows_[i];
    if (intensity <= 0.0) return 0.0;
    if (intensity > 1.0) intensity = 1.0;
    double lo = b.minDutyPct / 100.0;
    double hi = b.maxDutyPct / 100.0;
    return lo + intensity * (hi - lo);
}

void BowBank::applyDuty(int i) {
    if (i < 0 || i >= (int)bows_.size()) return;
#if defined(ARDUINO)
    // A disabled bank / bridge must output nothing regardless of the stored duty.
    uint32_t raw = enabled_ ? dutyToRaw(rt_[i].curDuty) : 0;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    if (pins_[i].pwm >= 0) ledcWrite(pins_[i].pwm, raw);
#else
    if (ledcCh_[i] >= 0) ledcWrite(ledcCh_[i], raw);
#endif
#endif
}

void BowBank::setIntensity(int i, double intensity, uint32_t nowMs) {
    if (i < 0 || i >= (int)bows_.size()) return;
    const BowConfig& b = bows_[i];
    if (!b.enabled) return;
    if (intensity <= 0.0) { stop(i, nowMs); return; }

    Rt& r = rt_[i];
    double tgt = targetDutyFor(i, intensity);
    if (!r.running) {
        // Starting a fresh note: pick the bowing direction (alternate every note
        // for a natural down-bow / up-bow) and set the DIR line before spinning up.
        r.dirLevel = b.reverse ^ (b.alternate && r.parity);
#if defined(ARDUINO)
        if (pins_[i].dir >= 0) digitalWrite(pins_[i].dir, r.dirLevel ? HIGH : LOW);
#endif
        if (b.alternate) r.parity = !r.parity;
        r.running = true;
        r.rampMs = b.spinUpMs;
    } else {
        // Live modulation while sounding: keep the direction, ease to the new duty.
        r.rampMs = b.spinUpMs;
    }
    r.startDuty = r.curDuty;
    r.targetDuty = tgt;
    r.rampStartMs = nowMs;
}

void BowBank::stop(int i, uint32_t nowMs) {
    if (i < 0 || i >= (int)bows_.size()) return;
    Rt& r = rt_[i];
    if (!r.running && r.curDuty <= 0.0) return;
    r.startDuty = r.curDuty;
    r.targetDuty = 0.0;
    r.rampMs = bows_[i].spinDownMs;
    r.rampStartMs = nowMs;
    // r.running stays true until the ramp reaches zero (see update()).
}

void BowBank::stopAll() {
    // Immediate hard stop (panic / E-stop): zero every duty now, no ramp.
    for (size_t i = 0; i < rt_.size(); ++i) {
        rt_[i].curDuty = 0.0;
        rt_[i].targetDuty = 0.0;
        rt_[i].startDuty = 0.0;
        rt_[i].running = false;
        applyDuty(static_cast<int>(i));
    }
}

void BowBank::update(uint32_t nowMs) {
    for (size_t i = 0; i < bows_.size(); ++i) {
        Rt& r = rt_[i];
        if (r.curDuty == r.targetDuty) {
            if (r.targetDuty <= 0.0) r.running = false;
            continue;  // steady state: nothing to write
        }
        if (r.rampMs == 0) {
            r.curDuty = r.targetDuty;
        } else {
            int32_t elapsed = static_cast<int32_t>(nowMs - r.rampStartMs);
            if (elapsed < 0) elapsed = 0;
            double t = static_cast<double>(elapsed) / r.rampMs;
            if (t >= 1.0) {
                r.curDuty = r.targetDuty;
            } else {
                r.curDuty = r.startDuty + (r.targetDuty - r.startDuty) * t;
            }
        }
        applyDuty(static_cast<int>(i));
        if (r.curDuty == r.targetDuty && r.targetDuty <= 0.0) r.running = false;
    }
}

void BowBank::enable(bool on) {
    enabled_ = on;
#if defined(ARDUINO)
    if (enablePin_ >= 0) digitalWrite(enablePin_, on ? HIGH : LOW);
#endif
    if (!on) {
        // Cut every PWM output immediately; keep the stored duties so a re-enable
        // does not resurrect a note (they are reset by stopAll on panic).
        for (size_t i = 0; i < bows_.size(); ++i) {
            rt_[i].curDuty = 0.0;
            rt_[i].targetDuty = 0.0;
            rt_[i].running = false;
            applyDuty(static_cast<int>(i));
        }
    }
}

int BowBank::indexForString(int stringIndex) const {
    for (size_t i = 0; i < bows_.size(); ++i)
        if (bows_[i].enabled && bows_[i].stringIndex == stringIndex)
            return static_cast<int>(i);
    return -1;
}

}  // namespace gmb
