#pragma once

// Applying MC damage to a hostile enemy through the game's own hit pipeline. Platform independent: the one game
// call is injected as `Invoker`, everything else (validation, HitContext construction, queueing, re-entrancy,
// expiry) is unit-tested against fake memory.
//
// Evidence (docs/ELDENRING_REVERSE.md 3.6): calling the victim's CSChrDamageModule::vfunc[7] ONCE, with the main
// player as attacker and a HitContext built from a real hit template, does damage, block, stagger, sound, aggro and
// death/kill credit natively. Do NOT also call ProcessDamageContext / vfunc[12] / 0x140446080 (double damage,
// forced stagger). The call must come from the game thread inside the update phase; this module only enforces
// "on_game_thread" as stated by the caller, it cannot prove it.

#include "eldenring_world.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace eldenring::live {

namespace layout {
inline constexpr uintptr_t kDamageModuleSlot = 19; // A (0x13)
inline constexpr uint32_t kEnemyDamageModuleVtableRva = 0x2A3A0D8;
inline constexpr const char* kEnemyDamageModuleRtti = ".?AVCSEnemyDamageModule@CS@@";
inline constexpr unsigned kDamageVfuncIndex = 7; // ProcessAttackHit

inline constexpr size_t kHitTemplateSize = 0x240; // bytes copied from a real player->enemy hit
inline constexpr size_t kHitContextSize = 0x280;  // the engine reads beyond the template (+0x264)
// HitContext fields (B+: a real hit captured, and visible effects with A/B tests).
inline constexpr size_t kHitStaggerLevel = 0x067;    // u8
inline constexpr size_t kHitStaggerAnim = 0x0DA;     // u8 (2 -> 0x3EB, light recoil; 6/7/8 untested)
inline constexpr size_t kHitHostileFlag = 0x11C;     // u8 (no effect on ordinary enemies, harmless)
inline constexpr size_t kHitPosition = 0x198;        // 3 x float, world (Havok) space
inline constexpr size_t kHitAttacker = 0x1D8;        // ChrIns* of the attacker
inline constexpr size_t kHitVictim = 0x1E0;          // ChrIns* of the victim == [victimDamageModule + 8]
inline constexpr size_t kHitDamage = 0x228;          // u32; the engine overwrites it with the computed damage
inline constexpr size_t kHitEnableReaction = 0x264;  // u8; 0 -> only HP drops: no flinch, no aggro

inline constexpr uint8_t kDefaultStaggerLevel = 3;
inline constexpr uint8_t kDefaultStaggerAnim = 2;
inline constexpr float kHitHeightAboveFeet = 1.0f;   // waist
} // namespace layout

// The 576 bytes of a real player->enemy hit (hit_7.bin). Loaded by the loader from a local file; never committed.
class HitTemplate {
public:
    static std::optional<HitTemplate> fromBytes(const uint8_t* data, size_t size) {
        if (data == nullptr || size != layout::kHitTemplateSize) return std::nullopt;
        HitTemplate t;
        std::memcpy(t.bytes_.data(), data, size);
        return t;
    }
    [[nodiscard]] const uint8_t* data() const { return bytes_.data(); }

private:
    std::array<uint8_t, layout::kHitTemplateSize> bytes_{};
};

// 16-byte aligned context, as the engine passes it on the stack.
struct alignas(16) HitContext {
    uint8_t bytes[layout::kHitContextSize];
};

inline HitContext buildHitContext(const HitTemplate& tmpl, uintptr_t attacker_chr, uintptr_t victim_chr, int32_t base_damage,
                                  const float victim_pos[3]) {
    HitContext c;
    std::memset(c.bytes, 0, sizeof(c.bytes));
    std::memcpy(c.bytes, tmpl.data(), layout::kHitTemplateSize);
    const auto put = [&](size_t off, const auto& v) { std::memcpy(c.bytes + off, &v, sizeof(v)); };
    put(layout::kHitDamage, static_cast<uint32_t>(base_damage));
    put(layout::kHitStaggerLevel, layout::kDefaultStaggerLevel);
    put(layout::kHitStaggerAnim, layout::kDefaultStaggerAnim);
    put(layout::kHitEnableReaction, static_cast<uint8_t>(1));
    put(layout::kHitHostileFlag, static_cast<uint8_t>(1));
    const float pos[3] = {victim_pos[0], victim_pos[1] + layout::kHitHeightAboveFeet, victim_pos[2]};
    std::memcpy(c.bytes + layout::kHitPosition, pos, sizeof(pos));
    const uint64_t attacker = attacker_chr, victim = victim_chr;
    put(layout::kHitAttacker, attacker);
    put(layout::kHitVictim, victim);
    return c;
}

enum class DamageOutcome {
    Applied,
    Deferred,        // victim is not the entity being updated right now; stays queued
    Expired,
    NoTemplate,
    NoPlayer,
    VictimGone,   // unreadable, wrong class, owner mismatch
    WrongModule,  // damage module is not CSEnemyDamageModule
    VictimDead,   // hp <= 0 (a dead target re-runs the death check)
    NotHostile,
    BadDamage,
};

struct DamageRequest {
    uintptr_t victim_chr{0};
    int32_t base_damage{0};
    uint64_t enqueued_tick{0};
};

struct DamageResult {
    DamageRequest request;
    DamageOutcome outcome{DamageOutcome::VictimGone};
};

// The single game call: victim's CSChrDamageModule, attacker ChrIns, HitContext. On Windows this reads vtable[7]
// of `damage_module` and calls it; tests pass a fake.
using DamageInvoker = std::function<void(uintptr_t damage_module, uintptr_t attacker_chr, void* hit_context)>;

struct DrainContext {
    const IMemoryReader* reader{nullptr};
    uintptr_t image_base{0};
    bool on_game_thread{false};
    uint64_t tick{0};
    // The CSChrDataModule currently being updated on this thread (the ClampHP hook's rcx). With
    // `require_victim_updating` only that entity's requests run, so an entity is never changed while a worker
    // updates it; requests for other entities wait. 0 disables the restriction.
    uintptr_t updating_data_module{0};
    bool require_victim_updating{true};
    const HitTemplate* hit_template{nullptr};
    DamageInvoker invoke;
    size_t max_per_drain{4};
    uint64_t expire_after_ticks{120};
};

class DamageQueue {
public:
    static constexpr size_t kCapacity = 64;

    bool enqueue(uintptr_t victim_chr, int32_t base_damage, uint64_t tick) {
        std::lock_guard<std::mutex> g(m_);
        if (q_.size() >= kCapacity) return false;
        q_.push_back({victim_chr, base_damage, tick});
        return true;
    }
    [[nodiscard]] size_t pending() const {
        std::lock_guard<std::mutex> g(m_);
        return q_.size();
    }
    [[nodiscard]] uint64_t reentrantRejections() const { return reentrant_.load(); }

    // Runs at most `max_per_drain` due requests. Never throws away a request that merely has to wait.
    std::vector<DamageResult> drain(const DrainContext& ctx) {
        std::vector<DamageResult> results;
        if (!ctx.on_game_thread || ctx.reader == nullptr || !ctx.invoke) {
            return results;
        }
        bool expected = false;
        if (!in_drain_.compare_exchange_strong(expected, true)) {
            ++reentrant_;
            return results;
        }
        struct Release {
            std::atomic<bool>& f;
            ~Release() { f.store(false); }
        } release{in_drain_};

        std::deque<DamageRequest> work;
        {
            std::lock_guard<std::mutex> g(m_);
            work.swap(q_);
        }
        std::deque<DamageRequest> keep;
        size_t applied = 0;
        for (const DamageRequest& r : work) {
            if (ctx.tick > r.enqueued_tick && ctx.tick - r.enqueued_tick > ctx.expire_after_ticks) {
                results.push_back({r, DamageOutcome::Expired});
                continue;
            }
            if (applied >= ctx.max_per_drain) {
                keep.push_back(r);
                continue;
            }
            const DamageOutcome o = run(r, ctx);
            if (o == DamageOutcome::Deferred) {
                keep.push_back(r);
                continue;
            }
            if (o == DamageOutcome::Applied) ++applied;
            results.push_back({r, o});
        }
        {
            std::lock_guard<std::mutex> g(m_);
            // Requests that arrived during the drain go after the ones kept for later, in arrival order.
            for (const auto& arrived : q_) keep.push_back(arrived);
            q_.swap(keep);
        }
        return results;
    }

private:
    static DamageOutcome run(const DamageRequest& r, const DrainContext& ctx) {
        const IMemoryReader& rd = *ctx.reader;
        const uintptr_t base = ctx.image_base;
        if (ctx.hit_template == nullptr) return DamageOutcome::NoTemplate;
        if (r.base_damage <= 0 || r.base_damage > 100000) return DamageOutcome::BadDamage;

        uint64_t world = 0, player = 0;
        if (!rd.read(base + layout::kWorldChrManGlobalRva, &world, sizeof(world)) || world == 0 ||
            !rd.read(static_cast<uintptr_t>(world) + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0) {
            return DamageOutcome::NoPlayer;
        }
        const auto player_chr = static_cast<uintptr_t>(player);
        const uintptr_t victim = r.victim_chr;
        if (victim == 0 || victim == player_chr) return DamageOutcome::VictimGone;
        if (!objectIsClass(rd, base, victim, layout::kEnemyInsVtableRva, ".?AVEnemyIns@CS@@")) return DamageOutcome::VictimGone;

        uint64_t container = 0, data_module = 0, damage_module = 0;
        if (!rd.read(victim + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0) {
            return DamageOutcome::VictimGone;
        }
        const auto cont = static_cast<uintptr_t>(container);
        if (!rd.read(cont + layout::kChrDataModuleSlot * sizeof(uint64_t), &data_module, sizeof(data_module)) || data_module == 0 ||
            !rd.read(cont + layout::kDamageModuleSlot * sizeof(uint64_t), &damage_module, sizeof(damage_module)) ||
            damage_module == 0) {
            return DamageOutcome::VictimGone;
        }
        if (ctx.require_victim_updating && ctx.updating_data_module != 0 &&
            ctx.updating_data_module != static_cast<uintptr_t>(data_module)) {
            return DamageOutcome::Deferred;
        }
        if (ctx.require_victim_updating && ctx.updating_data_module == 0) return DamageOutcome::Deferred;

        const auto dmg = static_cast<uintptr_t>(damage_module);
        uint64_t owner = 0;
        if (!rd.read(dmg + layout::kOwnerInDataModule, &owner, sizeof(owner)) || owner != victim) return DamageOutcome::VictimGone;
        if (!objectIsClass(rd, base, dmg, layout::kEnemyDamageModuleVtableRva, layout::kEnemyDamageModuleRtti)) {
            return DamageOutcome::WrongModule;
        }

        Vitals v;
        if (!readVitals(rd, base, victim, v)) return DamageOutcome::VictimGone;
        if (v.hp <= 0) return DamageOutcome::VictimDead;

        uint8_t player_team = 0, victim_team = 0;
        if (!rd.read(player_chr + layout::kTeamTypeInChrIns, &player_team, 1) ||
            !rd.read(victim + layout::kTeamTypeInChrIns, &victim_team, 1)) {
            return DamageOutcome::VictimGone;
        }
        if (readTeamRelation(rd, base, player_team, victim_team) != TeamRelation::Hostile) return DamageOutcome::NotHostile;

        float pos[3];
        if (!detail::readPhysicsPosition(rd, base, victim, pos)) return DamageOutcome::VictimGone;

        HitContext hit = buildHitContext(*ctx.hit_template, player_chr, victim, r.base_damage, pos);
        ctx.invoke(dmg, player_chr, hit.bytes);
        return DamageOutcome::Applied;
    }

    mutable std::mutex m_;
    std::deque<DamageRequest> q_;
    std::atomic<bool> in_drain_{false};
    std::atomic<uint64_t> reentrant_{0};
};

} // namespace eldenring::live
