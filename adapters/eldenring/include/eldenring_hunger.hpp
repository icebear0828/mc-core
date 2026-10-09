#pragma once

// Minecraft's food rules for the Elden Ring adapter: the food level and saturation, the exhaustion that wears them down, the foods,
// and what the food level does (natural regeneration, starvation). 20 ticks per second, as in the game. Pure logic, unit-tested.

#include "mc/types.hpp"

#include <algorithm>

namespace eldenring::live {

struct FoodValue {
    int nutrition{0};
    float saturation{0.f}; // absolute saturation points (Minecraft 1.21 food properties)
    bool always_edible{false};
};

inline FoodValue foodValue(mc::ItemId item) {
    switch (item) {
        case mc::ItemId::Bread: return {5, 6.0f, false};
        case mc::ItemId::CookedBeef: return {8, 12.8f, false};
        case mc::ItemId::GoldenApple: return {4, 9.6f, true};
        case mc::ItemId::EnchantedGoldenApple: return {4, 9.6f, true};
        default: return {};
    }
}

class HungerModel {
public:
    static constexpr int kMaxFood = 20;

    [[nodiscard]] int food() const { return food_; }
    [[nodiscard]] float saturation() const { return saturation_; }
    // The hunger icons wobble once the saturation is used up (Gui.renderFood).
    [[nodiscard]] bool shaking() const { return saturation_ <= 0.f && food_ > 0; }

    [[nodiscard]] bool canEat(mc::ItemId item) const {
        const FoodValue v = foodValue(item);
        if (v.nutrition == 0) return false;
        return food_ < kMaxFood || v.always_edible;
    }
    void eat(mc::ItemId item) {
        const FoodValue v = foodValue(item);
        if (v.nutrition == 0) return;
        food_ = std::min(kMaxFood, food_ + v.nutrition);
        saturation_ = std::min(saturation_ + v.saturation, static_cast<float>(food_));
    }

    // Exhaustion: every 4.0 costs one saturation point, or one food point when no saturation is left.
    void addExhaustion(float amount) {
        exhaustion_ += amount;
        while (exhaustion_ >= 4.f) {
            exhaustion_ -= 4.f;
            if (saturation_ > 0.f) saturation_ = std::max(0.f, saturation_ - 1.f);
            else food_ = std::max(0, food_ - 1);
        }
    }

    // Advances `dt` seconds; `hurt` = the player is below full health. Returns the Minecraft hit points to heal.
    // Full food + saturation: min(saturation, 6) / 6 points every 10 ticks; food >= 18: 1 point every 80 ticks; both cost exhaustion.
    float tick(float dt, bool hurt) {
        tick_accum_ += static_cast<double>(dt) * 20.0;
        const int ticks = static_cast<int>(tick_accum_);
        tick_accum_ -= ticks;
        float healed = 0.f;
        for (int i = 0; i < ticks; ++i) {
            if (hurt && food_ >= kMaxFood && saturation_ > 0.f) {
                starve_ticks_ = 0;
                if (++regen_ticks_ >= 10) {
                    const float f = std::min(saturation_, 6.f);
                    healed += f / 6.f;
                    addExhaustion(f);
                    regen_ticks_ = 0;
                }
            } else if (hurt && food_ >= 18) {
                starve_ticks_ = 0;
                if (++regen_ticks_ >= 80) {
                    healed += 1.f;
                    addExhaustion(6.f);
                    regen_ticks_ = 0;
                }
            } else if (food_ <= 0) {
                regen_ticks_ = 0;
                if (++starve_ticks_ >= 80) {
                    ++starvation_hits_;
                    starve_ticks_ = 0;
                }
            } else {
                regen_ticks_ = 0;
                starve_ticks_ = 0;
            }
        }
        return healed;
    }
    // Starvation hits since the last call (1 hit point each in Minecraft; the caller decides whether to apply them).
    int takeStarvationHits() {
        const int n = starvation_hits_;
        starvation_hits_ = 0;
        return n;
    }

private:
    int food_{kMaxFood};
    float saturation_{5.f};
    float exhaustion_{0.f};
    double tick_accum_{0.0};
    int regen_ticks_{0};
    int starve_ticks_{0};
    int starvation_hits_{0};
};

} // namespace eldenring::live
