#pragma once

#include "mc/types.hpp"
#include <array>

namespace mc {

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
    // Issue #64: riptide dash progress in [0, 1] over the 0.6 s loop (0 = not riptiding).
    float riptide{0.f};
    // Issue #65: elytra wing-flap phase in [0, 1] (0 = wings at rest). Reserved for the #38/#39 wing
    // props; the 12-part rig has no wing bones, so it does not affect the pose yet.
    float elytra_wing_flap{0.f};
};

class SteveAnimator {
public:
    static constexpr size_t kPartCount = static_cast<size_t>(StevePart::Count);
    using PartTransforms = std::array<Transform, kPartCount>;

    SteveAnimator();

    void update(float dt, const SteveAnimInput& input);
    [[nodiscard]] const PartTransforms& getTransforms() const { return transforms_; }

    // Whole-body pose that the flat 12-part rig cannot express on its own (the parts have no parent/child
    // links). `rootTransform()` is the root translation plus the rig rotation about the hip;
    // `upperTransform()` is the torso rotation about the same hip. Both are canonical centimetres /
    // canonical quaternions. A consumer post-multiplies them in world space (upper for the torso parts,
    // root for every part) exactly like the death/sleep poses.
    [[nodiscard]] const Transform& rootTransform() const { return root_; }
    [[nodiscard]] const Transform& upperTransform() const { return upper_; }

    void reset();

private:
    float age_{0.f};
    float stride_{0.f};
    float glide_age_{0.f}; // seconds spent in the elytra glide; drives the 1.6 s bob
    Transform root_{};
    Transform upper_{};
    PartTransforms transforms_{};
};

} // namespace mc
