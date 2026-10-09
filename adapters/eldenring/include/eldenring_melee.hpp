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
    // Stack sizes as in Session::loadDefaultHotbar. An emptied stack leaves the slot empty.
    [[nodiscard]] unsigned countAt(int slot) const { return counts_[static_cast<size_t>(std::clamp(slot, 0, kSlots - 1))]; }
    bool consumeAt(int slot) {
        if (slot < 0 || slot >= kSlots || counts_[static_cast<size_t>(slot)] == 0) return false;
        if (--counts_[static_cast<size_t>(slot)] == 0) items_[static_cast<size_t>(slot)] = mc::ItemId::None;
        return true;
    }
    // Takes one of `item` from the first slot that has it (the totem works from anywhere in the hotbar).
    bool consumeFirst(mc::ItemId item) {
        for (int i = 0; i < kSlots; ++i) {
            if (items_[static_cast<size_t>(i)] == item && counts_[static_cast<size_t>(i)] > 0) return consumeAt(i);
        }
        return false;
    }
    [[nodiscard]] bool has(mc::ItemId item) const {
        for (int i = 0; i < kSlots; ++i) {
            if (items_[static_cast<size_t>(i)] == item && counts_[static_cast<size_t>(i)] > 0) return true;
        }
        return false;
    }
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
    std::array<unsigned, kSlots> counts_{1, 1, 64, 64, 16, 8, 1, 1, 1};
    int slot_{0};
    float cooldown_{1.f};
    float swing_{-1.f}; // seconds into the swing, < 0 = idle
};

// "Falling" for an MC critical hit. Elden Ring's jump has a ~0.8 s wind-up after the key press and then only ~0.1 s with
// PhysicsModule+0x92 == 0 (measured, mc_er.log 2026-10-09), so "airborne right now" almost never coincides with a click.
// A jump therefore runs from the jump key press (on the ground) to the landing; stepping off a ledge also counts.
class JumpTracker {
public:
    static constexpr float kWindUpTimeoutSec = 1.5f; // a press that never leaves the ground (cancelled, blocked) stops counting

    void update(float dt, bool jump_pressed, bool airborne) {
        if (airborne) {
            state_ = State::Air;
        } else if (state_ == State::Air) {
            state_ = State::Idle; // landed
        } else if (state_ == State::WindUp) {
            wind_up_ += dt;
            if (wind_up_ > kWindUpTimeoutSec) state_ = State::Idle;
        } else if (jump_pressed) {
            state_ = State::WindUp;
            wind_up_ = 0.f;
        }
    }
    [[nodiscard]] bool jumping() const { return state_ != State::Idle; }

private:
    enum class State { Idle, WindUp, Air };
    State state_{State::Idle};
    float wind_up_{0.f};
};

// Crosshair feedback for our own hits, all values 0..1 and fading by themselves. A kill marker outlives the hit marker.
class HitFeedback {
public:
    static constexpr float kHitSec = 0.25f;
    static constexpr float kKillSec = 0.6f;
    static constexpr float kTotemSec = 1.2f;
    static constexpr float kHurtSec = 0.5f; // Minecraft: hurtTime 10 ticks

    void onHit(bool critical) {
        hit_ = 1.f;
        crit_ = critical;
    }
    void onKill() { kill_ = 1.f; }
    void onTotem() { totem_ = 1.f; }
    void onHurt(float side) {
        hurt_ = 1.f;
        hurt_side_ = side < 0.f ? -1.f : 1.f;
    }
    void tick(float dt) {
        hit_ = std::max(0.f, hit_ - dt / kHitSec);
        kill_ = std::max(0.f, kill_ - dt / kKillSec);
        totem_ = std::max(0.f, totem_ - dt / kTotemSec);
        hurt_ = std::max(0.f, hurt_ - dt / kHurtSec);
        if (hit_ <= 0.f) crit_ = false;
    }
    [[nodiscard]] float hit() const { return hit_; }
    [[nodiscard]] bool crit() const { return crit_ && hit_ > 0.f; }
    [[nodiscard]] float kill() const { return kill_; }
    [[nodiscard]] float totem() const { return totem_; }
    [[nodiscard]] float hurt() const { return hurt_; }       // 1 just hurt .. 0
    [[nodiscard]] float hurtSide() const { return hurt_side_; }

private:
    float hit_{0.f};
    float kill_{0.f};
    float totem_{0.f};
    float hurt_{0.f};
    float hurt_side_{1.f};
    bool crit_{false};
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
