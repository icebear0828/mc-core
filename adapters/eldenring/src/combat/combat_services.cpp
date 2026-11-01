#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

ShieldVerdict EvaluateShield(uintptr_t player, uintptr_t attacker_chr) {
    return eldenring::combat::CombatService::Instance().evaluateShield(player, attacker_chr);
}

bool PlayerHitSurvival(void* module, void* attacker, uint8_t* ctx, uint8_t blocked, uint32_t& popped_heal_target) {
    std::lock_guard<std::mutex> g(g_melee_mutex);
    auto& r_ctx = eldenring::RuntimeContext::Instance();
    r_ctx.mode().shield_blocking.store(g_shield_blocking.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return eldenring::combat::CombatService::Instance().playerHitSurvival(
        module, attacker, ctx, blocked, popped_heal_target,
        g_melee, g_survival, g_feedback, g_hunger, r_ctx);
}

void HandleProjectileHit(void* module, void* attacker, uint8_t* ctx) {
    eldenring::combat::CombatService::Instance().handleProjectileHit(
        module, attacker, ctx, g_feedback, eldenring::RuntimeContext::Instance());
}

void DrainOnce(uintptr_t updating_data_module) {
    eldenring::combat::CombatService::Instance().drainDamageQueue(
        g_queue, g_template ? &*g_template : nullptr, updating_data_module, NowTick(),
        g_melee.inventory(), g_feedback, g_xp, eldenring::RuntimeContext::Instance());
}

void EnqueueNearestHostile() {
    eldenring::combat::CombatService::Instance().enqueueNearestHostile(g_queue, NowTick());
}

bool ClickAttack(float charged) {
    return eldenring::combat::CombatService::Instance().clickAttack(
        charged, g_melee, g_combat_engine, g_queue, eldenring::RuntimeContext::Instance());
}

} // namespace eldenring::loader_detail
