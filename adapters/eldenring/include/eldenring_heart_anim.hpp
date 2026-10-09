#pragma once

// The heart row's animation as Gui.renderHearts does it: a damage blink (the container flashes and the hearts that were just lost show
// white for a second), the regeneration wave (one heart jumps and the jump travels along the row) and the shaking of the hearts at two
// hearts or less. Time is counted in Minecraft ticks (20 per second); health is in Minecraft hit points (half hearts). Pure logic.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eldenring::live {

class HeartAnimator {
public:
    static constexpr int kHearts = 10;

    // One frame: `dt` seconds passed; health and absorption in half hearts; `regenerating` = the Regeneration effect is active.
    void update(float dt, int health, int absorption, bool regenerating) {
        tick_accum_ += static_cast<double>(dt) * 20.0;
        const int ticks = static_cast<int>(tick_accum_);
        tick_accum_ -= ticks;
        for (int i = 0; i < ticks; ++i) {
            ++tick_;
            if (health + absorption <= 4) {
                for (int h = 0; h < kHearts; ++h) shake_[h] = static_cast<int8_t>((nextRandom() >> 16) & 1);
            } else {
                for (int& s : shake_) s = 0;
            }
        }
        regen_index_ = regenerating ? static_cast<int>(tick_ % 25) : -1; // ceil(max health 20 + 5) positions
        if (last_health_ < 0) {
            last_health_ = display_ = health;
            last_change_tick_ = tick_;
        }
        if (health < last_health_) { // hurt: the blink window opens and the old health stays on show
            last_change_tick_ = tick_;
            blink_until_ = tick_ + 20;
        }
        if (tick_ - last_change_tick_ > 20) { // a second after the last change the display catches up
            display_ = health;
            last_change_tick_ = tick_;
        }
        last_health_ = health;
        blink_ = blink_until_ > tick_ && ((blink_until_ - tick_) / 3) % 2 == 1;
    }

    [[nodiscard]] bool blink() const { return blink_; }
    // The health the white hearts reach up to while blinking (the health before the hit).
    [[nodiscard]] int displayHealth() const { return display_; }
    // The heart that is up in the regeneration wave (can be 10..24, i.e. none of the visible ones), or -1 without the effect.
    [[nodiscard]] int regenIndex() const { return regen_index_; }
    // 0 or 1 pixel of random vertical shake for heart `i` (only at 2 hearts or less).
    [[nodiscard]] int shake(int i) const { return i >= 0 && i < kHearts ? shake_[i] : 0; }

private:
    uint32_t nextRandom() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return rng_;
    }

    double tick_accum_{0.0};
    int tick_{0};
    int last_health_{-1};
    int display_{0};
    int last_change_tick_{0};
    int blink_until_{0};
    bool blink_{false};
    int regen_index_{-1};
    int shake_[kHearts]{};
    uint32_t rng_{12345u};
};

} // namespace eldenring::live
