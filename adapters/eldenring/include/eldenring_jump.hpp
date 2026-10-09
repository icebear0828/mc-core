#pragma once

// Minecraft's jump numbers and a small recorder for the jump experiment (mc_jump=1): the peak and the air time of a jump, and the gravity
// the game applies, estimated from the vertical speed it shows. Pure logic.

#include <algorithm>
#include <cmath>

namespace eldenring::live {

inline constexpr float kMcJumpSpeed = 8.4f; // 0.42 blocks per tick, 20 ticks per second
inline constexpr float kMcGravity = 32.f;   // 0.08 blocks per tick squared

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
    // One frame: the feet height, the frame time, and optionally the vertical speed the game reports.
    void sample(float y, float dt, float vy = NAN) {
        t_ += dt;
        peak_ = std::max(peak_, y - y0_);
        if (!std::isnan(vy)) {
            if (have_v_ && dt > 1e-4f) {
                g_sum_ += (prev_v_ - vy) / dt; // the speed falls by g every second
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

} // namespace eldenring::live
