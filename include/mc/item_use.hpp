#pragma once

// The right-click item family: one table says how an item is used, and one resolver decides
// which hand owns a right click.
//
// Minecraft tries the main hand first and lets the offhand act only when the main hand's use
// does not consume the click. That is an outcome rule, not a property of the item type: a block
// in the main hand consumes the click only when it is actually placed. The resolver below takes
// a per-hand availability snapshot, so the interaction handler no longer keeps a per-item
// if-chain and a new right-click item is one row in useSpec().
//
// Pure logic, unit-tested.

#include "mc/blocks.hpp"
#include "mc/hand_items.hpp"
#include "mc/hunger.hpp"
#include "mc/types.hpp"

#include <optional>

namespace mc {

enum class UseKind : uint8_t {
    None = 0,
    Place,       // place a block (instant, repeats while the button is held)
    Eat,         // eat food (hold)
    Bow,         // draw and release a bow (hold)
    Shield,      // raise a shield (hold)
    Trident,     // charge and release a trident (hold)
    Ignite,      // flint and steel on TNT or a creeper (instant)
    ThrowPearl,  // throw an ender pearl (instant)
    SpawnEgg,    // use a spawn egg (instant)
    Firework     // boost an elytra glide (instant)
};

// How one item uses the right button. `presentation` is the pose the hands take while the use is
// active; instant uses have no pose of their own.
struct UseSpec {
    UseKind kind{UseKind::None};
    bool instant{false};
    bool repeats{false};
    bool hold{false};
    HandUse presentation{HandUse::None};
};

// The one table. A new right-click item adds a row here, not a branch in the input handler.
[[nodiscard]] inline UseSpec useSpec(ItemId item) {
    switch (item) {
        case ItemId::Bow: return {UseKind::Bow, false, false, true, HandUse::BowDraw};
        case ItemId::Trident: return {UseKind::Trident, false, false, true, HandUse::TridentCharge};
        case ItemId::Shield: return {UseKind::Shield, false, false, true, HandUse::Block};
        case ItemId::FlintAndSteel: return {UseKind::Ignite, true, false, false, HandUse::None};
        case ItemId::EnderPearl: return {UseKind::ThrowPearl, true, false, false, HandUse::None};
        case ItemId::FireworkRocket: return {UseKind::Firework, true, false, false, HandUse::None};
        case ItemId::ZombieSpawnEgg:
        case ItemId::CreeperSpawnEgg:
        case ItemId::SkeletonSpawnEgg: return {UseKind::SpawnEgg, true, false, false, HandUse::None};
        default:
            if (foodValue(item).nutrition > 0) return {UseKind::Eat, false, false, true, HandUse::Eat};
            if (blockForItem(item) != BlockId::Air) return {UseKind::Place, true, true, false, HandUse::None};
            return {};
    }
}

[[nodiscard]] constexpr bool isHoldUse(UseKind kind) noexcept {
    return kind == UseKind::Eat || kind == UseKind::Bow || kind == UseKind::Shield || kind == UseKind::Trident;
}

// What one hand can do with the click right now. The adapter fills this in; the resolver never
// looks at the world. A kind that is unavailable lets the other hand try.
struct UseAvailability {
    bool place{false};
    bool eat{false};
    bool bow{false};
    bool shield{false};
    bool trident{false};
    bool ignite{false};
    bool throw_pearl{false};
    bool spawn_egg{false};
    bool firework{false};
};

[[nodiscard]] constexpr bool canStartUse(UseKind kind, const UseAvailability& available) noexcept {
    switch (kind) {
        case UseKind::Place: return available.place;
        case UseKind::Eat: return available.eat;
        case UseKind::Bow: return available.bow;
        case UseKind::Shield: return available.shield;
        case UseKind::Trident: return available.trident;
        case UseKind::Ignite: return available.ignite;
        case UseKind::ThrowPearl: return available.throw_pearl;
        case UseKind::SpawnEgg: return available.spawn_egg;
        case UseKind::Firework: return available.firework;
        case UseKind::None: return false;
    }
    return false;
}

struct UseResolution {
    Hand hand{Hand::Main};
    UseKind kind{UseKind::None};
};

// Main hand first, offhand only when the main hand has nothing that can consume this click.
[[nodiscard]] inline std::optional<UseResolution> resolveUse(ItemId main_item, ItemId off_item,
                                                             const UseAvailability& main_available,
                                                             const UseAvailability& off_available) {
    const ItemId items[2] = {main_item, off_item};
    const UseAvailability available[2] = {main_available, off_available};
    for (int i = 0; i < 2; ++i) {
        const UseSpec spec = useSpec(items[i]);
        if (spec.kind == UseKind::None) continue;
        if (!canStartUse(spec.kind, available[i])) continue;
        return UseResolution{i == 0 ? Hand::Main : Hand::Off, spec.kind};
    }
    return std::nullopt;
}

} // namespace mc
