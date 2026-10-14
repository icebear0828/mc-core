#pragma once

// Minecraft-authentic sprint controller:
// - Double-tap 'W' within 300ms window activates sprint
// - Holding Ctrl while moving forward instantly activates sprint
// - Releasing forward ('W') or pressing backward ('S') cancels sprint
// - Hunger requirement: food level must be > 6 (more than 3 shanks) to sprint
// - Dynamic FOV scaling (+15% with smooth lerp interpolation)
// - Animation clock multiplier (1.4x leg/arm swing frequency)

#include <algorithm>
#include <cmath>

namespace eldenring::sprint {

class SprintController {
public:
    static constexpr float kDoubleTapWindowSec = 0.300f; // 300ms double-tap timeout
    static constexpr int kMinFoodLevelForSprint = 6;    // food level <= 6 blocks sprint
    static constexpr float kFovSprintMultiplier = 1.15f; // +15% FOV on sprint
    static constexpr float kFovLerpSpeed = 15.0f;        // Smooth transition speed
    static constexpr float kAnimSprintMultiplier = 1.4f; // 1.4x swing frequency

    SprintController() = default;

    void update(float dt, bool w_down, bool s_down, bool ctrl_down, int food_level) {
        // Condition checks
        const bool hunger_allows = food_level > kMinFoodLevelForSprint;
        const bool moving_backward = s_down;

        if (!hunger_allows || moving_backward || !w_down) {
            is_sprinting_ = false;
        }

        // Double-tap W detection
        if (!prev_w_down_ && w_down) {
            // W Key Down event
            if (w_tapped_once_ && (time_since_w_released_ <= kDoubleTapWindowSec) && hunger_allows && !moving_backward) {
                is_sprinting_ = true;
                w_tapped_once_ = false;
            }
        } else if (prev_w_down_ && !w_down) {
            // W Key Up event
            if (!is_sprinting_) {
                w_tapped_once_ = true;
                time_since_w_released_ = 0.0f;
            } else {
                is_sprinting_ = false;
                w_tapped_once_ = false;
            }
        }

        // Tapped timeout tracking
        if (w_tapped_once_) {
            time_since_w_released_ += dt;
            if (time_since_w_released_ > kDoubleTapWindowSec) {
                w_tapped_once_ = false;
            }
        }

        // Ctrl single key override
        if (ctrl_down && w_down && !moving_backward && hunger_allows) {
            is_sprinting_ = true;
        }

        prev_w_down_ = w_down;

        // FOV smooth interpolation
        const float target_fov = is_sprinting_ ? kFovSprintMultiplier : 1.0f;
        const float alpha = std::clamp(dt * kFovLerpSpeed, 0.0f, 1.0f);
        fov_multiplier_ += (target_fov - fov_multiplier_) * alpha;
    }

    [[nodiscard]] bool isSprinting() const { return is_sprinting_; }
    [[nodiscard]] float fovMultiplier() const { return fov_multiplier_; }
    [[nodiscard]] float animSpeedMultiplier() const {
        return is_sprinting_ ? kAnimSprintMultiplier : 1.0f;
    }

    void reset() {
        is_sprinting_ = false;
        prev_w_down_ = false;
        w_tapped_once_ = false;
        time_since_w_released_ = 0.0f;
        fov_multiplier_ = 1.0f;
    }

private:
    bool is_sprinting_{false};
    bool prev_w_down_{false};
    bool w_tapped_once_{false};
    float time_since_w_released_{0.0f};
    float fov_multiplier_{1.0f};
};

} // namespace eldenring::sprint
