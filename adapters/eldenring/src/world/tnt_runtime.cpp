#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

void ProcessTntExplosionsOnGameThread(const std::vector<EnemyInfo>& enemies) {
    eldenring::world::VoxelWorldRuntime::Instance().processTntExplosions(enemies, g_ctx);
}

void EnemiesBlockCollisionStep(const float player_feet[3]) {
    const bool mc_on = g_mc_mode.load(std::memory_order_relaxed);
#if ENABLE_CS2_VIEWMODEL
    const bool cs_on = eldenring::RuntimeContext::Instance().mode().cs_mode.load(std::memory_order_relaxed);
#else
    const bool cs_on = false;
#endif
    // OW strikes trace against the same cached enemy list. Without this the
    // cache is only refreshed for MC/CS, and a punch in an OW-only build finds
    // no targets at all.
    const bool ow_on = eldenring::RuntimeContext::Instance().mode().ow_mode.load(std::memory_order_relaxed);
    if (!mc_on && !cs_on && !ow_on) return;

    std::vector<EnemyInfo> enemies;
    if (!enumerateEnemies(g_reader, g_img.base, enemies, 512)) {
        EntityManager::instance().clearCache();
        return;
    }

    // The CS hitscan and the OW strikes read this cache on the input thread.
    EntityManager::instance().updateCacheFromGameThread(enemies);

    if (!mc_on) return;

    // Process queued TNT explosion damage intents on game thread!
    ProcessTntExplosionsOnGameThread(enemies);

    eldenring::world::VoxelWorldRuntime::Instance().stepEnemyBlockCollision(player_feet, g_ctx);
}

void TriggerTntExplosionEffectsAndDamage(const float center[3]) {
    eldenring::world::VoxelWorldRuntime::Instance().triggerTntExplosion(center, g_ctx);
}

void BlocksTick() {
    static bool prev_loading = false;
    static unsigned pushes = 0;
    LoadingState ls;
    if (readLoadingState(g_reader, readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper), ls)) {
        if (ls.screen_loading && !prev_loading) {
            std::lock_guard<std::mutex> g(g_blocks_mutex);
            if (g_blocks.count() != 0) Log("blocks: loading screen, %zu blocks forgotten", g_blocks.count());
            g_blocks.clear();
            g_tnt_mgr.clear();
            g_virtual_summons.dismissAll();
        }
        prev_loading = ls.screen_loading;
    }

    // Step active primed TNT
    static uint64_t last_tnt_ms = 0;
    const uint64_t now_ms = GetTickCount64();
    if (last_tnt_ms == 0) last_tnt_ms = now_ms;
    const float dt = std::clamp(static_cast<float>(now_ms - last_tnt_ms) / 1000.f, 0.001f, 0.1f);
    last_tnt_ms = now_ms;

    std::vector<eldenring::tnt::ErExplosionResult> explosions;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (g_tnt_mgr.activeCount() > 0) {
            explosions = g_tnt_mgr.step(dt, g_blocks);
        }
    }

    // Trigger effects and sounds outside of g_blocks_mutex to eliminate lock recursion
    for (const auto& exp : explosions) {
        TriggerTntExplosionEffectsAndDamage(exp.center);
    }

    SendBlockMesh();
    if (!g_camstep_installed.load()) BlocksCollisionStep(); // without the camera hook the collision runs here (less smooth)
}

} // namespace eldenring::loader_detail
