#pragma once

#include "mc/types.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace mc {

struct HudSlot {
    ItemId item{ItemId::None};
    uint32_t count{0};
};

struct HeartContainers {
    int full_hearts{0};
    bool has_half_heart{false};
    int empty_hearts{0};
};

struct HungerContainers {
    int full_drumsticks{0};
    bool has_half_drumstick{false};
    int empty_drumsticks{0};
};

class HudEngine {
public:
    static constexpr int kHotbarSlotCount = 9;

    HudEngine();

    // Hotbar slots
    void selectSlot(int index);
    void scrollSlot(int delta);
    [[nodiscard]] int getSelectedSlot() const { return selected_slot_; }

    void setSlot(int index, ItemId item, uint32_t count = 1);
    [[nodiscard]] const HudSlot& getSlot(int index) const;
    [[nodiscard]] ItemId getSelectedItem() const;
    [[nodiscard]] uint32_t getSelectedSlotCount() const;
    [[nodiscard]] const char* getSelectedItemDisplayName() const;

    // Health & Hunger state
    void setHealth(float health);
    void setMaxHealth(float max_health);
    [[nodiscard]] float getHealth() const { return health_; }
    [[nodiscard]] float getMaxHealth() const { return max_health_; }

    void setHunger(float hunger);
    [[nodiscard]] float getHunger() const { return hunger_; }

    // Crosshair visibility
    void setCrosshairVisible(bool visible) { crosshair_visible_ = visible; }
    [[nodiscard]] bool isCrosshairVisible() const { return crosshair_visible_; }

    // Container math helpers for rendering
    [[nodiscard]] HeartContainers computeHearts() const;
    [[nodiscard]] HungerContainers computeHunger() const;

    static const char* getItemDisplayName(ItemId item);

private:
    std::array<HudSlot, kHotbarSlotCount> slots_{};
    int selected_slot_{0};
    float health_{20.0f};
    float max_health_{20.0f};
    float hunger_{20.0f};
    bool crosshair_visible_{true};
};

} // namespace mc
