#pragma once

// SSBU-style ground mining (issue #66/#81): dig the host's terrain under the player's feet and
// roll the zone loot table. Pure logic: the adapter supplies the camera pitch, the held item and
// the zone; this class owns the gating, the 10-stage crack timing and the loot roll.

#include "mc/loot_table.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace mc {

struct GroundMiningState {
    bool is_mining{false};
    float progress{0.0f}; // 0..1
    int stage{-1};        // -1 = none, 0..9 = crack stage
    bool broken{false};   // true on the tick the ground gives way
    bool hit_sound{false};
    LootRoll drop{};      // valid when broken
};

class GroundMiningController {
public:
    static constexpr float kMinPitchDeg = 60.0f; // the aim must be at the ground at the player's feet
    static constexpr float kMinGroundNormalY = 0.5f; // the hit surface must face up, not a wall
    static constexpr float kHitSoundIntervalSec = 0.25f;

    explicit GroundMiningController(std::span<const LootEntry> entries = kDefaultLootTable) : loot_(entries) {}

    [[nodiscard]] static bool canDig(ItemId held, float pitch_deg) noexcept {
        return pitch_deg >= kMinPitchDeg && (held == ItemId::None || isPickaxe(held));
    }

    // A wall hit (normal mostly horizontal) is not ground: the crack decal would stand up above the
    // floor. The adapter feeds the terrain ray's normal here before starting a dig.
    [[nodiscard]] static bool canDigSurface(float normal_y) noexcept { return normal_y >= kMinGroundNormalY; }

    // Issue #81 section 2: bare hands are slow; each pickaxe tier is 30%..60% faster. Creative
    // breaks instantly. The zone hardness keeps the deeper regions from feeling like grass.
    [[nodiscard]] static float breakTime(ItemId held, LootZone zone, bool creative) noexcept {
        if (creative) return 0.0f;
        return tierSeconds(toolTier(held)) * zoneHardness(zone);
    }

    // dt seconds this frame; pitch_deg is the downward angle of the aim (0 = level, 90 = straight down).
    // `moving` is the adapter's read of the player's own motion: issue #81 interrupts the dig the
    // moment the player moves, so the crack never follows them across the terrain.
    void update(float dt, bool left_held, float pitch_deg, ItemId held, LootZone zone, bool creative, uint64_t& rng_state,
                bool moving = false) {
        state_.broken = false;
        state_.hit_sound = false;
        state_.drop = {};

        if (!left_held || moving || !canDig(held, pitch_deg)) {
            resetProgress();
            return;
        }

        // Changing the tool or the zone starts a fresh crack; the loot streak (pity/combo) survives.
        if (state_.is_mining && (held != mining_tool_ || zone != mining_zone_)) {
            resetProgress();
        }

        const float break_time = breakTime(held, zone, creative);
        if (!state_.is_mining) {
            state_.is_mining = true;
            mining_tool_ = held;
            mining_zone_ = zone;
            state_.progress = 0.0f;
            state_.stage = 0;
            state_.hit_sound = true;
            hit_timer_ = 0.0f;
        }

        if (break_time <= 0.001f) {
            breakNow(held, zone, rng_state);
            return;
        }

        state_.progress += dt / break_time;
        if (state_.progress >= 1.0f) {
            state_.progress = 1.0f;
            breakNow(held, zone, rng_state);
            return;
        }

        state_.stage = std::clamp(static_cast<int>(state_.progress * 10.0f), 0, 9);
        hit_timer_ += dt;
        if (hit_timer_ >= kHitSoundIntervalSec) {
            state_.hit_sound = true;
            hit_timer_ = std::fmod(hit_timer_, kHitSoundIntervalSec);
        }
    }

    // Releasing the button, moving or looking up: the crack disappears immediately. Pity and combo
    // are not part of the crack, so they stay.
    void reset() { resetProgress(); }

    [[nodiscard]] const GroundMiningState& state() const { return state_; }
    [[nodiscard]] LootTable& loot() { return loot_; }
    [[nodiscard]] const LootTable& loot() const { return loot_; }

private:
    static float tierSeconds(ToolTier tier) noexcept {
        switch (tier) {
            case ToolTier::Hand: return 2.5f; // ~50 ticks, issue #81's 45..60 tick bare-hand cycle
            case ToolTier::Wood: return 2.0f;
            case ToolTier::Stone: return 1.75f;
            case ToolTier::Iron: return 1.5f;
            case ToolTier::Gold: return 1.25f;
            case ToolTier::Diamond: return 1.0f; // 60% faster than bare hands
            case ToolTier::Netherite: return 0.9f;
            default: return 2.5f;
        }
    }

    static float zoneHardness(LootZone zone) noexcept {
        switch (zone) {
            case LootZone::Grassland: return 1.0f;
            case LootZone::StoneSurface: return 1.3f;
            case LootZone::Cave: return 1.5f;
            case LootZone::CrystalCave: return 1.7f;
            case LootZone::Volcano: return 1.8f;
            case LootZone::Deeproot: return 1.6f;
            default: return 1.0f;
        }
    }

    void breakNow(ItemId held, LootZone zone, uint64_t& rng_state) {
        state_.stage = 9;
        state_.broken = true;
        state_.is_mining = false;
        state_.drop = loot_.roll(zone, toolTier(held), rng_state);
        hit_timer_ = 0.0f;
    }

    void resetProgress() {
        state_.is_mining = false;
        state_.progress = 0.0f;
        state_.stage = -1;
        state_.broken = false;
        state_.hit_sound = false;
        state_.drop = {};
        hit_timer_ = 0.0f;
        mining_tool_ = ItemId::None;
        mining_zone_ = LootZone::Grassland;
    }

    LootTable loot_;
    GroundMiningState state_{};
    float hit_timer_{0.0f};
    ItemId mining_tool_{ItemId::None};
    LootZone mining_zone_{LootZone::Grassland};
};

} // namespace mc
