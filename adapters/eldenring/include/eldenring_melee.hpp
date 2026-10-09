#pragma once

// The Minecraft side of a melee attack on the real game: the hotbar (which item is held), the attack cooldown and the
// swing animation clock. Damage numbers come from mc::CombatEngine; erDamage turns the result into Elden Ring hit
// points. Pure logic, unit-tested without the game.

#include "mc/combat.hpp"
#include "mc/session.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace eldenring::live {

class MeleeController {
public:
    static constexpr int kSlots = 9;

    // Same hotbar as mc::Session::loadDefaultHotbar.
    MeleeController() {
        items_ = {mc::ItemId::DiamondSword, mc::ItemId::DiamondPickaxe, mc::ItemId::BlockDirt, mc::ItemId::BlockStone,
                  mc::ItemId::BlockTnt,     mc::ItemId::GoldenApple,    mc::ItemId::Bow,       mc::ItemId::Elytra,
                  mc::ItemId::TotemOfUndying};
    }

    [[nodiscard]] int selectedSlot() const { return slot_; }
    [[nodiscard]] mc::ItemId heldItem() const { return items_[static_cast<size_t>(slot_)]; }
    [[nodiscard]] mc::ItemId itemAt(int slot) const { return items_[static_cast<size_t>(std::clamp(slot, 0, kSlots - 1))]; }
    void select(int slot) {
        if (slot >= 0 && slot < kSlots) slot_ = slot;
    }
    void scroll(int steps) { slot_ = ((slot_ + steps) % kSlots + kSlots) % kSlots; }

    [[nodiscard]] float cooldown() const { return cooldown_; }
    [[nodiscard]] float swingProgress() const { return swing_ < 0.f ? 0.f : swing_ / mc::Session::kSwingDurationSec; }

    void tick(float dt) {
        cooldown_ = std::min(1.f, cooldown_ + dt / mc::Session::kAttackRechargeSec);
        if (swing_ >= 0.f) {
            swing_ += dt;
            if (swing_ >= mc::Session::kSwingDurationSec) swing_ = -1.f;
        }
    }

    // A click: the arm swings and the cooldown restarts. Returns the cooldown the swing was made with (the hit's
    // strength), whether or not it connects.
    float startSwing() {
        const float charged = cooldown_;
        swing_ = 0.f;
        cooldown_ = 0.f;
        return charged;
    }

    [[nodiscard]] mc::HitIntent makeIntent(mc::CombatEngine& engine, mc::EntityId attacker, mc::EntityId victim, float charged,
                                           bool falling, bool on_ground, const mc::Vec3& where, const mc::Vec3& direction) const {
        return engine.calculateMeleeHit(attacker, victim, heldItem(), charged, falling, on_ground, where, direction);
    }

private:
    std::array<mc::ItemId, kSlots> items_{};
    int slot_{0};
    float cooldown_{1.f};
    float swing_{-1.f}; // seconds into the swing, < 0 = idle
};

// Elden Ring hit points for a landed MC hit: a full diamond-sword hit (7 damage) is `max_hp_percent` of the enemy's
// maximum health; everything else scales linearly with MC damage. At least 1 for a landed hit; 0 when the enemy's
// maximum health is unknown.
inline int erDamage(const mc::HitIntent& intent, int victim_max_hp) {
    if (victim_max_hp <= 0) return 0;
    constexpr float kReferenceDamage = 7.f;
    const float hp = static_cast<float>(victim_max_hp) * intent.max_hp_percent * (intent.damage / kReferenceDamage);
    return std::max(1, static_cast<int>(std::lround(hp)));
}

} // namespace eldenring::live
