#pragma once

// Damage conversion and health mirroring between Minecraft and a host game, following SkyCraft's
// design (DESIGN.md 8.2, 8.3 and 13.1).
//
// The two games use unrelated health scales: a diamond sword deals 7 MC damage while a host NPC may
// have hundreds of hit points. Two configurable rules keep them coherent:
//   MC -> host: host_damage = mc_damage * (base + per_level * host_level)
//   host -> MC: mc_damage   = host_damage / divisor
// HealthMirror keeps one health bar mirrored onto the other as a fraction in [0, 1], with one side
// authoritative and a hysteresis band so small floating-point drift does not spam updates.
//
// Engine-free: standard library only.

#include "mc/combat.hpp"

namespace mc {

// SkyCraft's defaults (DESIGN.md 13.1): a diamond sword crit of ~10 MC damage hits a level-10 host
// NPC for ~75, and a 20-damage host hit becomes 4 MC damage (2 hearts).
struct DamageScalingConfig {
    static constexpr float kDefaultMcToHostBase = 5.0f;
    static constexpr float kDefaultMcToHostPerLevel = 0.25f;
    static constexpr float kDefaultHostToMcDivisor = 5.0f;
    // A smaller divisor would turn any host hit into an absurd MC hit.
    static constexpr float kMinHostToMcDivisor = 1e-3f;

    float mc_to_host_base{kDefaultMcToHostBase};
    float mc_to_host_per_level{kDefaultMcToHostPerLevel};
    float host_to_mc_divisor{kDefaultHostToMcDivisor};

    // Sanitised copy: negative or non-finite values fall back to the defaults and the divisor is
    // kept at or above kMinHostToMcDivisor, so every conversion stays finite.
    [[nodiscard]] DamageScalingConfig validated() const;
};

// host damage = mc damage * (base + per_level * level). Levels below zero count as zero.
[[nodiscard]] float mcToHostMultiplier(int host_level, const DamageScalingConfig& config);

// Both clamp negative and non-finite input to zero and always return a finite value.
[[nodiscard]] float scaleMcToHost(float mc_damage, int host_level, const DamageScalingConfig& config);
[[nodiscard]] float scaleHostToMc(float host_damage, const DamageScalingConfig& config);

// Scales a HitIntent's absolute damage and its max-HP percentage by the same multiplier, so the two
// fields keep describing one hit. Every other field is untouched.
void applyScaledDamage(HitIntent& intent, int host_level, const DamageScalingConfig& config);

// Mirrors one health bar onto the other. The authoritative side owns the health; the other side
// follows it as a fraction of its own maximum.
class HealthMirror {
public:
    enum class Authority {
        Minecraft, // MC health is the truth (the player): the host bar follows it
        Host,      // the host health is the truth (an NPC proxy): the MC bar follows it
    };

    static constexpr float kDefaultMaxHp = 20.0f; // a MC player's ten hearts
    static constexpr float kPushEpsilon = 0.01f;  // 1% of max HP: smaller drift is not worth a packet
    static constexpr float kDeathFraction = 1e-4f;
    static constexpr float kReviveFraction = 0.01f; // must climb back above this to clear death

    HealthMirror() = default;
    explicit HealthMirror(Authority authority, float max_hp = kDefaultMaxHp);

    [[nodiscard]] Authority authority() const { return authority_; }
    // Re-seeds the mirror from the new source of truth and forces the next remote push.
    void setAuthority(Authority authority);

    [[nodiscard]] float maxHp() const { return max_hp_; }
    // Changing the maximum does not fire a death event; it only re-clamps the current health.
    void setMaxHp(float max_hp);

    [[nodiscard]] float localHp() const { return local_hp_; }
    // Drives the death latch under Minecraft authority; under Host authority the local bar is a
    // mirror and observeRemoteHp overwrites it.
    void setLocalHp(float hp);

    // Stores the host's health fraction. Under Host authority it also drives the local bar and the
    // death latch; under Minecraft authority it is informational only.
    void observeRemoteHp(float fraction);

    [[nodiscard]] float localFraction() const;
    // The host-side fraction: what the host reported, or what was last pushed to it.
    [[nodiscard]] float remoteFraction() const { return remote_fraction_; }

    [[nodiscard]] bool isDead() const { return dead_; }
    [[nodiscard]] bool justDied() const { return just_died_; }
    // Returns the one-shot death flag and clears it.
    bool consumeDeath();

    // True when the host bar is at least kPushEpsilon away from the last pushed value. Always false
    // under Host authority: the remote already owns the value.
    [[nodiscard]] bool shouldPushRemoteUpdate(float new_fraction) const;
    // Records the current local fraction as pushed and resets the baseline.
    void markPushed();

    // Clears the death latch and forces a resync; the caller supplies the new health afterwards.
    void revive();

private:
    void updateDeathLatch(float authoritative_fraction);

    Authority authority_{Authority::Minecraft};
    float max_hp_{kDefaultMaxHp};
    float local_hp_{kDefaultMaxHp};
    float remote_fraction_{1.0f};
    float last_pushed_fraction_{1.0f};
    bool dead_{false};
    bool just_died_{false};
};

} // namespace mc
