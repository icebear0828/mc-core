#pragma once

#include "mc/particles.hpp"
#include "mc/types.hpp"
#include <array>

namespace mc {

// Vanilla Minecraft's third-person hurt roll (GameRenderer.bobHurt): up to 14 degrees away from the damage
// source, damped back to upright over 10 ticks (0.5 s at 20 TPS). `onHurt` starts the timer; `update` decays
// it; `tiltRadians` converts the remaining time into the signed roll the animator applies to the rig.
class HurtTiltTracker {
public:
    static constexpr float kMaxRadians = 14.f * 0.01745329252f; // ~0.2443 rad
    static constexpr float kDecaySec = 0.5f;                    // 10 ticks

    // `side` is the damage source's side: -1 = attacker on the character's left, anything else = on its right.
    void onHurt(float side) {
        side_ = side < 0.f ? -1.f : 1.f;
        timer_ = 1.f;
    }
    void update(float dt) {
        if (timer_ <= 0.f) return;
        timer_ -= dt / kDecaySec;
        if (timer_ < 0.f) timer_ = 0.f;
    }
    // Signed roll in radians: away from the damage source, following the vanilla sin(t^4 * pi) curve.
    [[nodiscard]] float tiltRadians() const { return hurtTiltRadians(timer_, side_); }
    [[nodiscard]] float timer() const { return timer_; } // 1 just hurt .. 0 upright
    [[nodiscard]] float side() const { return side_; }
    void reset() {
        timer_ = 0.f;
        side_ = 1.f;
    }

private:
    float timer_{0.f};
    float side_{1.f};
};

enum class StevePart : uint8_t {
    Head = 0,
    Body,
    RightArm,
    LeftArm,
    RightLeg,
    LeftLeg,
    Hat,
    Jacket,
    RightSleeve,
    LeftSleeve,
    RightPants,
    LeftPants,
    Count
};

struct SteveAnimInput {
    float forward_speed{0.f}; // m/s
    float strafe_speed{0.f};  // m/s
    float look_yaw{0.f};      // radians, head yaw RELATIVE to the body (0 = facing the body's heading)
    float look_pitch{0.f};    // radians, Minecraft convention: positive = looking DOWN
    float swing_progress{0.f};// [0, 1] 0 = idle, 1 = completed
    bool is_crouching{false};
    bool is_sprinting{false};
    bool on_ground{true};
    float vertical_speed{0.f}; // m/s, >0 rising, <0 falling
    bool is_blocking{false};
    bool block_offhand{false}; // the shield is held in the left hand: only the left arm is raised (ArmPose.BLOCK), otherwise only the right one
    float bow_charge{0.f};    // [0, 1]
    bool bow_offhand{false};  // the bow is held in the left hand (the left arm aims instead of the right one)
    float trident_charge{0.f};// [0, 1]
    float eating_progress{0.f}; // [0, 1] 0 = not eating, >0 = chewing vibration
    bool arms_forward{false}; // zombie on the Steve rig: both arms held straight out in front
    bool arms_sway_only{false}; // zombie from a model file whose rest rotation already holds the arms out: only a slow sway on top
    bool is_gliding{false};
    float roll_angle{0.f};    // radians (banking roll when turning during flight)
    bool is_sleeping{false};
    float hurt_tilt{0.f};     // radians, signed roll about the body's forward axis (vanilla hurt tilt, up to 14 deg)
};

class SteveAnimator {
public:
    static constexpr size_t kPartCount = static_cast<size_t>(StevePart::Count);
    using PartTransforms = std::array<Transform, kPartCount>;

    SteveAnimator();

    void update(float dt, const SteveAnimInput& input);
    [[nodiscard]] const PartTransforms& getTransforms() const { return transforms_; }

    void reset();

private:
    float age_{0.f};
    float stride_{0.f};
    PartTransforms transforms_{};
};

} // namespace mc
