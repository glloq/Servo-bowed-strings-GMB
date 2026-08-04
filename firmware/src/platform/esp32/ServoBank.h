// Servo bank supporting PCA9685 (up to four boards) AND direct-GPIO servos,
// mixable per servo (user requirement: work with or without a PCA). Roles:
// finger (fret press) and bowPress (bow descent / contact force) per string,
// plus aux actuators. The string is set in vibration by the DC bow motor
// (BowBank), not by a servo, so there is no pluck / strum / damper here.
// The PCA /OE line is tied to a safety pin so all PCA servos can be neutralised
// instantly (spec §21.2); direct servos are detached on stop.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../core/configuration/Profile.h"

#if defined(ARDUINO)
#include <Adafruit_PWMServoDriver.h>
#include <Arduino.h>
#endif

namespace gmb {

class ServoBank {
public:
    static constexpr int kMaxPca = 4;  // 0x40..0x43

    // sda/scl select the I2C pins; oePin drives /OE (active-low). Any of them may
    // be -1 when unused (e.g. no PCA at all — all servos on direct GPIO).
    void begin(const std::vector<ServoConfig>& servos, int8_t sda, int8_t scl,
               int8_t oePin);

    void toRest(int index);
    void toActive(int index);
    void toMicros(int index, uint16_t us);

    // Non-blocking motion helpers (honour travelMs / settleMs / disableAtRest):
    //   press     : hold active   (finger down / bow at full force)
    //   pressForce: hold a contact force between rest and active, scaled by
    //               intensity 0..1 (the bowPress descent servo; velocity/CC7/CC11)
    //   release   : return to rest (finger up / bow lifted), then optionally cut
    //               PWM at rest
    // Returns false if the servo could not actually be driven (LEDC re-attach or
    // PCA write failure) so the caller can fault the axis (audit P1-5).
    bool press(int index);
    bool pressForce(int index, double intensity);
    void release(int index);
    // Advance rest-time PWM cut-off. Call from loop().
    void update(uint32_t nowMs);

    // Hardware safety: enable/disable all PCA outputs via /OE.
    void outputEnable(bool on);
    void neutraliseAll();

    // Lookup by role + string (-1 for shared roles). Returns -1 if absent.
    int servoIndex(const std::string& function, int stringIndex) const;
    int fingerIndex(int stringIndex) const { return servoIndex("finger", stringIndex); }
    // The bow descent servo that lowers the bow onto the string and sets the
    // contact force (rest = lifted, active = full force).
    int bowPressIndex(int stringIndex) const { return servoIndex("bowPress", stringIndex); }

    // True if any configured direct-GPIO servo failed to attach an LEDC channel.
    bool directAttachFault() const { return directAttachFault_; }
    // True if a referenced PCA9685 board did not respond on I2C.
    bool pcaAttachFault() const { return pcaAttachFault_; }
    // Runtime health probe: re-checks that every used PCA9685 still ACKs on I2C,
    // so a board unplugged AFTER arming is detected (returns true when no PCA is
    // used). Cheap enough to call a few times a second.
    bool pcaHealthy() const;

    size_t count() const { return servos_.size(); }
    // How many LEDC channels the direct-GPIO servos consumed, so the bow bank can
    // allocate the remaining channels without colliding (2.x LEDC API).
    int directChannelsUsed() const { return directCount_; }
    // True if `index` refers to a real, enabled servo that can actually be driven
    // (so a web servo-test can reject an invalid/disabled index instead of
    // silently succeeding).
    bool commandable(int index) const {
        return index >= 0 && index < static_cast<int>(servos_.size()) &&
               servos_[index].enabled;
    }
    bool usesPca() const { return pcaUsed_; }
    uint16_t travelMs(int index) const {
        return (index >= 0 && index < (int)servos_.size()) ? servos_[index].travelMs : 0;
    }
    uint16_t settleMs(int index) const {
        return (index >= 0 && index < (int)servos_.size()) ? servos_[index].settleMs : 0;
    }

private:
    enum class Mode : uint8_t { Rest, Active };
    struct Rt {
        Mode mode = Mode::Rest;
        uint32_t restAtMs = 0;    // when a resting servo may cut its PWM
        bool pwmOff = false;
    };
    std::vector<Rt> rt_;

    std::vector<ServoConfig> servos_;
    std::vector<int8_t> ledcCh_;  // LEDC channel per direct servo (Arduino 2.x)
    std::vector<bool> attached_;  // direct-servo LEDC attach state
    int8_t oePin_ = -1;
    int directCount_ = 0;         // number of LEDC channels handed out
    bool pcaUsed_ = false;
    bool pcaPresent_[kMaxPca] = {false, false, false, false};
    bool directAttachFault_ = false;
    bool pcaAttachFault_ = false;
    static constexpr int kMaxDirectServos = 8;  // ESP32-S3 has 8 LEDC channels
#if defined(ARDUINO)
    Adafruit_PWMServoDriver pca_[kMaxPca] = {
        Adafruit_PWMServoDriver(0x40), Adafruit_PWMServoDriver(0x41),
        Adafruit_PWMServoDriver(0x42), Adafruit_PWMServoDriver(0x43)};
#endif
    bool writeMicros(int index, uint16_t us);  // false if the write couldn't apply
    void writeOff(int index);
    bool attachDirect(int index);  // (re)attach a direct servo's LEDC channel
};

}  // namespace gmb
