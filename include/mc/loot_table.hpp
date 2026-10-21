#pragma once

// Data-driven weighted loot tables for mining the host's ground (issue #81).
// Pure logic: zones, weights, tool tiers, the pity counter and the vein combo. No host access.
//
// The table is a span of entries so an adapter or a test can supply its own data. Weights are
// percentage points per zone and are normalised at roll time: the issue #81 table is the default,
// and its volcano column currently sums to 80.5 rather than 100, which normalisation absorbs.

#include "mc/types.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace mc {

enum class LootZone : uint8_t {
    Grassland = 0, // open grass and dirt
    StoneSurface,  // stone ground, castles
    Cave,          // ordinary mines and tunnels
    CrystalCave,   // crystal tunnels (for example the Sellia Crystal Tunnel)
    Volcano,       // volcanic underground
    Deeproot,      // deeproot depths and the Eternal City
    Count
};

inline constexpr int kLootZoneCount = static_cast<int>(LootZone::Count);

enum class ToolTier : uint8_t {
    Hand = 0,
    Wood,
    Stone,
    Iron,
    Gold,
    Diamond,
    Netherite
};

inline constexpr bool isPickaxe(ItemId item) noexcept {
    switch (item) {
        case ItemId::WoodenPickaxe:
        case ItemId::StonePickaxe:
        case ItemId::IronPickaxe:
        case ItemId::GoldenPickaxe:
        case ItemId::DiamondPickaxe:
        case ItemId::NetheritePickaxe: return true;
        default: return false;
    }
}

inline constexpr ToolTier toolTier(ItemId item) noexcept {
    switch (item) {
        case ItemId::WoodenPickaxe: return ToolTier::Wood;
        case ItemId::StonePickaxe: return ToolTier::Stone;
        case ItemId::IronPickaxe: return ToolTier::Iron;
        case ItemId::GoldenPickaxe: return ToolTier::Gold;
        case ItemId::DiamondPickaxe: return ToolTier::Diamond;
        case ItemId::NetheritePickaxe: return ToolTier::Netherite;
        default: return ToolTier::Hand;
    }
}

inline constexpr bool isUnderground(LootZone zone) noexcept {
    return zone == LootZone::Cave || zone == LootZone::CrystalCave || zone == LootZone::Volcano || zone == LootZone::Deeproot;
}

inline constexpr bool isMineral(ItemId item) noexcept {
    switch (item) {
        case ItemId::Coal:
        case ItemId::RawCopper:
        case ItemId::RawIron:
        case ItemId::Redstone:
        case ItemId::RawGold:
        case ItemId::LapisLazuli:
        case ItemId::AmethystShard:
        case ItemId::Diamond:
        case ItemId::Emerald:
        case ItemId::NetheriteScrap: return true;
        default: return false;
    }
}

struct LootEntry {
    ItemId item{ItemId::None};
    std::array<float, kLootZoneCount> weight{}; // percentage points; the zone total is normalised
    ToolTier min_tier{ToolTier::Hand};
};

// Issue #81 section 4. Order is the wheel order, which makes a seeded roll reproducible.
inline constexpr std::array<LootEntry, 12> kDefaultLootTable{{
    {ItemId::BlockDirt, {90.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f}, ToolTier::Hand},
    {ItemId::BlockCobblestone, {5.0f, 76.0f, 35.0f, 25.0f, 15.0f, 20.0f}, ToolTier::Hand},
    {ItemId::Coal, {2.0f, 8.0f, 18.0f, 5.0f, 10.0f, 12.0f}, ToolTier::Hand},
    {ItemId::RawCopper, {1.5f, 5.0f, 15.0f, 5.0f, 5.0f, 8.0f}, ToolTier::Stone},
    {ItemId::RawIron, {1.0f, 6.0f, 20.0f, 10.0f, 15.0f, 22.0f}, ToolTier::Stone},
    {ItemId::Redstone, {0.0f, 1.5f, 5.0f, 12.0f, 12.0f, 10.0f}, ToolTier::Iron},
    {ItemId::RawGold, {0.0f, 1.0f, 4.0f, 6.0f, 20.0f, 8.0f}, ToolTier::Iron},
    {ItemId::LapisLazuli, {0.0f, 0.3f, 1.5f, 15.0f, 2.0f, 8.0f}, ToolTier::Stone},
    {ItemId::AmethystShard, {0.0f, 0.0f, 0.0f, 18.0f, 0.0f, 2.0f}, ToolTier::Iron},
    {ItemId::Diamond, {0.0f, 0.2f, 1.5f, 3.5f, 1.0f, 4.0f}, ToolTier::Iron},
    {ItemId::Emerald, {0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 6.0f}, ToolTier::Iron},
    {ItemId::NetheriteScrap, {0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f}, ToolTier::Iron},
}};

struct LootRoll {
    ItemId item{ItemId::None};
    // The wheel picked an ore the held tool cannot harvest: issue #81 downgrades it to broken
    // stone rubble instead of rerolling, so the table's probabilities stay intact.
    bool tier_blocked{false};
    bool pity_triggered{false};
    // Set when the fourth consecutive underground dig triggered a vein combo.
    bool combo_triggered{false};
    ItemId bonus{ItemId::None};
};

// SplitMix64: deterministic, fast, and good enough for a weighted wheel. Tests seed it directly.
inline uint64_t nextLootRandom(uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

inline float lootUnitRandom(uint64_t& state) noexcept {
    return static_cast<float>(nextLootRandom(state) >> 40) * (1.0f / 16777216.0f); // [0, 1)
}

class LootTable {
public:
    static constexpr int kPityThreshold = 5;    // consecutive underground digs without iron
    static constexpr float kPityStep = 15.0f;   // percentage points added per further miss
    static constexpr float kPityMax = 75.0f;    // never let the pity bonus swallow the wheel
    static constexpr int kComboLength = 4;      // digs needed for a vein combo
    static constexpr float kComboBonusChance = 0.5f;

    explicit LootTable(std::span<const LootEntry> entries = kDefaultLootTable) : entries_(entries) {}

    [[nodiscard]] std::span<const LootEntry> entries() const { return entries_; }

    [[nodiscard]] float zoneWeightSum(LootZone zone) const {
        float sum = 0.0f;
        for (const LootEntry& e : entries_) sum += e.weight[zoneIndex(zone)];
        return sum;
    }

    // The issue #81 table is expected to sum to 100 per zone. Volcano is 80.5 in the issue;
    // roll() normalises, so a caller can decide whether to correct the data instead.
    [[nodiscard]] bool zonesSumTo100(float tolerance = 0.01f) const {
        for (int z = 0; z < kLootZoneCount; ++z) {
            if (std::abs(zoneWeightSum(static_cast<LootZone>(z)) - 100.0f) > tolerance) return false;
        }
        return true;
    }

    // Base wheel without pity or combo: the acceptance Monte Carlo samples this.
    [[nodiscard]] LootRoll rollBase(LootZone zone, ToolTier tier, uint64_t& rng_state) const {
        return pick(zone, tier, rng_state, 0.0f, false);
    }

    // Gameplay roll: underground pity counter plus the four-dig vein combo. Tests that isolate
    // the pity counter pass allow_combo = false.
    LootRoll roll(LootZone zone, ToolTier tier, uint64_t& rng_state, bool allow_combo = true) {
        LootRoll result;
        if (!isUnderground(zone)) {
            pity_misses_ = 0;
            combo_ = 0;
            return pick(zone, tier, rng_state, 0.0f, false);
        }

        const float pity_bonus = pityBonus();
        const bool combo_ready = allow_combo && combo_ + 1 >= kComboLength;
        result = pick(zone, tier, rng_state, pity_bonus, combo_ready);
        result.pity_triggered = pity_bonus > 0.0f;
        result.combo_triggered = combo_ready;

        if (result.item == ItemId::RawIron) {
            pity_misses_ = 0;
        } else {
            ++pity_misses_;
        }

        if (combo_ready) {
            combo_ = 0;
            if (lootUnitRandom(rng_state) < kComboBonusChance) {
                result.bonus = pickComboBonus(tier, rng_state);
            }
        } else {
            ++combo_;
        }
        return result;
    }

    void reset() {
        pity_misses_ = 0;
        combo_ = 0;
    }

    [[nodiscard]] int pityMisses() const { return pity_misses_; }
    [[nodiscard]] int combo() const { return combo_; }

    [[nodiscard]] float pityBonus() const {
        if (pity_misses_ < kPityThreshold) return 0.0f;
        const int extra = pity_misses_ - kPityThreshold + 1;
        return std::min(kPityStep * static_cast<float>(extra), kPityMax);
    }

private:
    static int zoneIndex(LootZone zone) {
        const int i = static_cast<int>(zone);
        return (i >= 0 && i < kLootZoneCount) ? i : 0;
    }

    LootRoll pick(LootZone zone, ToolTier tier, uint64_t& rng_state, float pity_bonus, bool mineral_only) const {
        const int zi = zoneIndex(zone);
        float total = 0.0f;
        for (const LootEntry& e : entries_) {
            if (mineral_only && !isMineral(e.item)) continue;
            total += weightOf(e, zi) + (e.item == ItemId::RawIron ? pity_bonus : 0.0f);
        }
        if (!(total > 0.0f)) return {};

        float r = lootUnitRandom(rng_state) * total;
        const LootEntry* chosen = nullptr;
        for (const LootEntry& e : entries_) {
            if (mineral_only && !isMineral(e.item)) continue;
            const float w = weightOf(e, zi) + (e.item == ItemId::RawIron ? pity_bonus : 0.0f);
            if (!(w > 0.0f)) continue;
            chosen = &e;
            r -= w;
            if (r < 0.0f) break;
        }
        if (chosen == nullptr) return {};

        LootRoll result;
        if (tier < chosen->min_tier) {
            result.item = ItemId::BlockCobblestone; // broken stone rubble, per issue #81 section 5
            result.tier_blocked = true;
        } else {
            result.item = chosen->item;
        }
        return result;
    }

    static float weightOf(const LootEntry& e, int zone_index) { return e.weight[zone_index]; }

    static ItemId pickComboBonus(ToolTier tier, uint64_t& rng_state) {
        // Coal is always available; redstone needs an iron pickaxe or better.
        if (tier < ToolTier::Iron) return ItemId::Coal;
        return lootUnitRandom(rng_state) < 0.5f ? ItemId::Coal : ItemId::Redstone;
    }

    std::span<const LootEntry> entries_;
    int pity_misses_{0};
    int combo_{0};
};

} // namespace mc
