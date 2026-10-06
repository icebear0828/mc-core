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
    float look_yaw{0.f};      // radians
    float look_pitch{0.f};    // radians
    float swing_progress{0.f};// [0, 1] 0 = idle, 1 = completed
    bool is_crouching{false};
    bool is_blocking{false};
    float bow_charge{0.f};    // [0, 1]
    float trident_charge{0.f};// [0, 1]
    float eating_progress{0.f}; // [0, 1] 0 = not eating, >0 = chewing vibration
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
