#include "mc/hud.hpp"
#include <algorithm>
#include <cmath>

namespace mc {

HudEngine::HudEngine() = default;

void HudEngine::selectSlot(int index) {
    selected_slot_ = std::clamp(index, 0, kHotbarSlotCount - 1);
}

void HudEngine::scrollSlot(int delta) {
    int next = (selected_slot_ + delta) % kHotbarSlotCount;
    if (next < 0) {
        next += kHotbarSlotCount;
    }
    selected_slot_ = next;
}

void HudEngine::setSlot(int index, ItemId item, uint32_t count) {
    if (index >= 0 && index < kHotbarSlotCount) {
        slots_[static_cast<size_t>(index)] = HudSlot{item, count};
    }
}

const HudSlot& HudEngine::getSlot(int index) const {
    static const HudSlot kEmptySlot{};
    if (index >= 0 && index < kHotbarSlotCount) {
        return slots_[static_cast<size_t>(index)];
    }
    return kEmptySlot;
}

ItemId HudEngine::getSelectedItem() const {
    return slots_[static_cast<size_t>(selected_slot_)].item;
}

uint32_t HudEngine::getSelectedSlotCount() const {
    return slots_[static_cast<size_t>(selected_slot_)].count;
}

const char* HudEngine::getSelectedItemDisplayName() const {
    return getItemDisplayName(getSelectedItem());
}

void HudEngine::setHealth(float health) {
    health_ = std::clamp(health, 0.0f, max_health_);
}

void HudEngine::setMaxHealth(float max_health) {
    max_health_ = std::max(1.0f, max_health);
    health_ = std::min(health_, max_health_);
}

void HudEngine::setHunger(float hunger) {
    hunger_ = std::clamp(hunger, 0.0f, 20.0f);
}

HeartContainers HudEngine::computeHearts() const {
    HeartContainers result{};
    // Standard MC: 20 HP = 10 heart containers (each container = 2 HP)
    int total_half_hearts = static_cast<int>(std::round(health_));
    total_half_hearts = std::clamp(total_half_hearts, 0, 20);

    result.full_hearts = total_half_hearts / 2;
    result.has_half_heart = (total_half_hearts % 2) != 0;
    int filled_containers = result.full_hearts + (result.has_half_heart ? 1 : 0);
    result.empty_hearts = std::max(0, 10 - filled_containers);

    return result;
}

HungerContainers HudEngine::computeHunger() const {
    HungerContainers result{};
    int total_half_drumsticks = static_cast<int>(std::round(hunger_));
    total_half_drumsticks = std::clamp(total_half_drumsticks, 0, 20);

    result.full_drumsticks = total_half_drumsticks / 2;
    result.has_half_drumstick = (total_half_drumsticks % 2) != 0;
    int filled_containers = result.full_drumsticks + (result.has_half_drumstick ? 1 : 0);
    result.empty_drumsticks = std::max(0, 10 - filled_containers);

    return result;
}

const char* HudEngine::getItemDisplayName(ItemId item) {
    switch (item) {
        case ItemId::DiamondSword: return "Diamond Sword";
        case ItemId::DiamondPickaxe: return "Diamond Pickaxe";
        case ItemId::Bow: return "Bow";
        case ItemId::Arrow: return "Arrow";
        case ItemId::Trident: return "Trident";
        case ItemId::FlintAndSteel: return "Flint and Steel";
        case ItemId::EnderPearl: return "Ender Pearl";
        case ItemId::GoldenApple: return "Golden Apple";
        case ItemId::EnchantedGoldenApple: return "Enchanted Golden Apple";
        case ItemId::Bread: return "Bread";
        case ItemId::CookedBeef: return "Steak";
        case ItemId::TotemOfUndying: return "Totem of Undying";
        case ItemId::Elytra: return "Elytra";
        case ItemId::FireworkRocket: return "Firework Rocket";
        case ItemId::BlockDirt: return "Dirt";
        case ItemId::BlockStone: return "Stone";
        case ItemId::BlockTnt: return "TNT";
        case ItemId::ZombieSpawnEgg: return "Zombie Spawn Egg";
        case ItemId::None:
        default: return "";
    }
}

} // namespace mc
