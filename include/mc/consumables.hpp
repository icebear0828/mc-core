#pragma once

#include "mc/types.hpp"
#include <vector>

namespace mc {

struct EatingEvent {
    bool chew_sound{false};
    bool play_burp_sound{false};
    bool completed{false};
    std::vector<ActiveEffect> effects;
    float health_restored{0.f};
};

class ConsumableSystem {
public:
    ConsumableSystem();

    // Start consuming an item (returns false if item is not edible)
    bool startEating(ItemId item);
    void cancel();

    // Progress the eating timer by dt
    EatingEvent update(float dt);

    [[nodiscard]] bool isEating() const { return is_eating_; }
    [[nodiscard]] float getProgress() const;
    [[nodiscard]] ItemId getCurrentItem() const { return current_item_; }

    static bool isEdible(ItemId item);

private:
    bool is_eating_{false};
    ItemId current_item_{ItemId::None};
    float elapsed_{0.f};
    float total_duration_{1.6f}; // 1.6s default (32 MC ticks)
    float chew_timer_{0.f};
};

} // namespace mc
