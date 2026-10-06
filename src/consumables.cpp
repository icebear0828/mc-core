#include "mc/consumables.hpp"
#include <algorithm>

namespace mc {

ConsumableSystem::ConsumableSystem() = default;

bool ConsumableSystem::isEdible(ItemId item) {
    switch (item) {
        case ItemId::GoldenApple:
        case ItemId::EnchantedGoldenApple:
        case ItemId::Bread:
        case ItemId::CookedBeef:
            return true;
        default:
            return false;
    }
}

bool ConsumableSystem::startEating(ItemId item) {
    if (!isEdible(item)) {
        return false;
    }

    is_eating_ = true;
    current_item_ = item;
    elapsed_ = 0.f;
    chew_timer_ = 0.2f; // initial delay for first chew sound
    total_duration_ = 1.6f; // standard MC eating duration

    return true;
}

void ConsumableSystem::cancel() {
    is_eating_ = false;
    current_item_ = ItemId::None;
    elapsed_ = 0.f;
    chew_timer_ = 0.f;
}

float ConsumableSystem::getProgress() const {
    if (!is_eating_ || total_duration_ <= 0.f) {
        return 0.f;
    }
    return std::min(1.0f, elapsed_ / total_duration_);
}

EatingEvent ConsumableSystem::update(float dt) {
    EatingEvent event;
    if (!is_eating_) {
        return event;
    }

    elapsed_ += dt;
    chew_timer_ -= dt;

    if (chew_timer_ <= 0.f) {
        event.chew_sound = true;
        chew_timer_ = 0.2f; // pulse chewing sound every 200ms
    }

    if (elapsed_ >= total_duration_) {
        event.completed = true;
        event.play_burp_sound = true;

        if (current_item_ == ItemId::GoldenApple) {
            // Regeneration II (5s)
            event.effects.push_back(ActiveEffect{
                .type = EffectType::Regeneration,
                .level = 2,
                .duration = 5.0f,
            });
            // Absorption I (120s, 4 extra HP / 2 golden hearts)
            event.effects.push_back(ActiveEffect{
                .type = EffectType::Absorption,
                .level = 1,
                .duration = 120.0f,
            });
        } else if (current_item_ == ItemId::EnchantedGoldenApple) {
            // Regeneration II (20s)
            event.effects.push_back(ActiveEffect{
                .type = EffectType::Regeneration,
                .level = 2,
                .duration = 20.0f,
            });
            // Absorption IV (120s, 16 extra HP)
            event.effects.push_back(ActiveEffect{
                .type = EffectType::Absorption,
                .level = 4,
                .duration = 120.0f,
            });
            // Resistance I (300s)
            event.effects.push_back(ActiveEffect{
                .type = EffectType::Resistance,
                .level = 1,
                .duration = 300.0f,
            });
            // Fire Resistance I (300s)
            event.effects.push_back(ActiveEffect{
                .type = EffectType::FireResistance,
                .level = 1,
                .duration = 300.0f,
            });
        } else if (current_item_ == ItemId::CookedBeef) {
            event.health_restored = 8.0f;
        } else if (current_item_ == ItemId::Bread) {
            event.health_restored = 5.0f;
        }

        // Reset state upon completion
        is_eating_ = false;
        current_item_ = ItemId::None;
    }

    return event;
}

} // namespace mc
