#pragma once

#include <algorithm>
#include <cmath>

namespace mc {

inline constexpr float kMcJumpSpeed = 8.4f;           // 0.42 blocks per tick, 20 ticks per second
inline constexpr float kMcGravity = 32.f;             // 0.08 blocks per tick squared
inline constexpr float kMcJumpInitialVelocity = 8.4f; // m/s: achieves authentic ~1.25m peak with drag
inline constexpr float kMcJumpSprintBoost = 1.15f;    // 15% horizontal speed boost during sprint jump (Minecraft authentic)
inline constexpr float kMcJumpGravity = 32.0f;        // m/s^2: authentic air-time curve (~0.45s to peak, ~0.85s total)
inline constexpr float kMcAirDrag = 0.98f;            // drag per 50ms tick
inline constexpr float kMaxJumpDt = 0.05f;            // 50ms max step limit to prevent alt-tab spikes
inline constexpr float kJumpPeakTarget = 1.252f;      // meters (1.25 blocks)

// The peak of a Minecraft jump, simulated tick by tick (gravity 0.08 and the 0.98 vertical drag).
inline float mcJumpPeakHeight() {
    float y = 0.f, vy = 0.42f, peak = 0.f;
    for (int i = 0; i < 40; ++i) {
        y += vy;
        peak = std::max(peak, y);
        vy = (vy - 0.08f) * 0.98f;
    }
    return peak;
}

class JumpTrace {
public:
    void start(float y) {
        y0_ = y;
        peak_ = 0.f;
        t_ = 0.f;
        have_v_ = false;
        n_g_ = 0;
        g_sum_ = 0.f;
    }
    void sample(float y, float dt, float vy = NAN) {
        t_ += dt;
        peak_ = std::max(peak_, y - y0_);
        if (!std::isnan(vy)) {
            if (have_v_ && dt > 1e-4f) {
                g_sum_ += (prev_v_ - vy) / dt;
                ++n_g_;
            }
            prev_v_ = vy;
            have_v_ = true;
        }
    }
    [[nodiscard]] float peak() const { return peak_; }
    [[nodiscard]] float seconds() const { return t_; }
    [[nodiscard]] float estimatedGravity() const { return n_g_ > 0 ? g_sum_ / static_cast<float>(n_g_) : 0.f; }

private:
    float y0_{0.f}, peak_{0.f}, t_{0.f}, prev_v_{0.f}, g_sum_{0.f};
    bool have_v_{false};
    int n_g_{0};
};

// Determines whether the player can take off for a jump.
// Unlocks jumping both on native ground and on placed voxel blocks, eliminating block jump deadlocks.
inline bool canTakeOff(bool ground_flag, bool supported_by_block, bool on_ground_atomic, bool is_dead) {
    if (is_dead) return false;
    return ground_flag || supported_by_block || on_ground_atomic;
}

// State container for a smooth Minecraft jump arc
struct JumpStepResult {
    float y{0.f};
    float vy{0.f};
    float vx{0.f};
    float vz{0.f};
    bool active{false};
};

class MinecraftJumpController {
public:
    void start(float start_y, float vx, float vz, bool sprinting, float jump_speed = kMcJumpInitialVelocity) {
        start_y_ = start_y;
        y_ = start_y;
        vy_ = jump_speed;
        peak_ = 0.f;
        active_ = true;

        // In Minecraft, jumping while sprinting applies horizontal momentum boost:
        if (sprinting) {
            vx_ = vx * kMcJumpSprintBoost;
            vz_ = vz * kMcJumpSprintBoost;
        } else {
            vx_ = vx;
            vz_ = vz;
        }
    }

    JumpStepResult update(float dt, float desired_dir_x, float desired_dir_z, float desired_speed, bool has_input) {
        JumpStepResult res{};
        if (!active_) return res;

        dt = std::clamp(dt, 0.001f, kMaxJumpDt);

        // In discrete Minecraft physics, position advances by current velocity, then gravity and drag are applied
        y_ += vy_ * dt;
        peak_ = std::max(peak_, y_ - start_y_);

        vy_ -= kMcJumpGravity * dt;
        const float drag = std::pow(kMcAirDrag, dt / 0.05f);
        vy_ *= drag;

        // Horizontal integration:
        // If player provides input in the air, allow steering while preserving momentum (air authority):
        if (has_input && desired_speed > 0.f) {
            const float steer_factor = std::clamp(dt * 10.0f, 0.0f, 1.0f);
            const float target_vx = desired_dir_x * desired_speed;
            const float target_vz = desired_dir_z * desired_speed;
            vx_ = vx_ + (target_vx - vx_) * steer_factor;
            vz_ = vz_ + (target_vz - vz_) * steer_factor;
        } else {
            // Air drag on horizontal speed without input (Minecraft 0.91 per tick)
            const float h_drag = std::pow(0.91f, dt / 0.05f);
            vx_ *= h_drag;
            vz_ *= h_drag;
        }

        res.y = y_;
        res.vy = vy_;
        res.vx = vx_;
        res.vz = vz_;
        res.active = true;
        return res;
    }

    void land(float landing_y) {
        y_ = landing_y;
        vy_ = 0.f;
        active_ = false;
    }

    void hitCeiling(float ceiling_y) {
        y_ = ceiling_y;
        if (vy_ > 0.f) vy_ = 0.f;
    }

    void cancel() {
        active_ = false;
        vy_ = 0.f;
    }

    [[nodiscard]] bool isActive() const { return active_; }
    [[nodiscard]] float currentY() const { return y_; }
    [[nodiscard]] float startY() const { return start_y_; }
    [[nodiscard]] float peak() const { return peak_; }
    [[nodiscard]] float vy() const { return vy_; }
    [[nodiscard]] float vx() const { return vx_; }
    [[nodiscard]] float vz() const { return vz_; }

private:
    float start_y_{0.f};
    float y_{0.f};
    float vy_{0.f};
    float vx_{0.f};
    float vz_{0.f};
    float peak_{0.f};
    bool active_{false};
};

using JumpController = MinecraftJumpController;

} // namespace mc
