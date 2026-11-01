#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

static eldenring::bullet::LogBudget g_bullet_budget(40);

void LogBulletRequest(void* manager, const void* request, const void* r9, void* ret_addr, uint32_t handle) {
    const unsigned seq = g_bullet_seen.fetch_add(1) + 1;
    if (!g_bullet_budget.take()) return;
    uint8_t body[eldenring::bullet::kRequestBytes] = {};
    if (!SafeCopy(reinterpret_cast<uintptr_t>(request), body, sizeof(body))) {
        Log("bullet #%u: the request at %p cannot be read", seq, request);
        return;
    }
    const eldenring::bullet::Fields f = eldenring::bullet::decode(body, sizeof(body));
    Log("bullet #%u: manager=%p request=%p r9=%p caller RVA 0x%llX tid=%lu -> handle 0x%08X", seq, manager, request, r9,
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ret_addr) - g_img.base), static_cast<unsigned long>(GetCurrentThreadId()), handle);
    Log("bullet #%u: owner=0x%016llX param row (+0x08)=0x%016llX id@+1C=%u flags@+44=0x%X pos=(%.2f %.2f %.2f) fwd=(%.3f %.3f %.3f)", seq,
        static_cast<unsigned long long>(f.owner), static_cast<unsigned long long>(f.row_id), f.id_at_1c, f.flags_at_44, f.position[0], f.position[1],
        f.position[2], f.forward[0], f.forward[1], f.forward[2]);
    for (const std::string& line : eldenring::bullet::hexDump(body, sizeof(body))) Log("bullet #%u:   %s", seq, line.c_str());
    uint8_t extra[0x40] = {};
    if (r9 != nullptr && SafeCopy(reinterpret_cast<uintptr_t>(r9), extra, sizeof(extra))) {
        for (const std::string& line : eldenring::bullet::hexDump(extra, sizeof(extra))) Log("bullet #%u: r9 %s", seq, line.c_str());
    }
    // A real request carries a non-zero pointer at +0xB0 (to an object on the caller's stack) that our replay sets to 0, and the replay flies along the shooter's
    // body instead of the matrix: does that object hold the aim? Dumped with its floats so a vector near the camera's forward can be spotted (REVERSE 35.11).
    {
        auto dump = [&](const char* label, uintptr_t at, size_t bytes) {
            std::vector<uint8_t> buf(bytes, 0);
            if (at == 0 || !SafeCopy(at, buf.data(), bytes)) {
                Log("bullet #%u: %s at %p cannot be read", seq, label, reinterpret_cast<void*>(at));
                return;
            }
            Log("bullet #%u: %s at %p (%zu bytes):", seq, label, reinterpret_cast<void*>(at), bytes);
            for (size_t row = 0; row < bytes; row += 16) {
                float v[4];
                std::memcpy(v, buf.data() + row, sizeof(v));
                char line[200];
                int n = snprintf(line, sizeof(line), "+0x%03zX:", row);
                for (size_t i = 0; i < 16; ++i) n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " %02X", buf[row + i]);
                snprintf(line + n, sizeof(line) - static_cast<size_t>(n), "  | %10.4f %10.4f %10.4f %10.4f", v[0], v[1], v[2], v[3]);
                Log("bullet #%u:   %s", seq, line);
            }
        };
        uint64_t sub = 0;
        std::memcpy(&sub, body + 0xB0, sizeof(sub));
        dump("sub-object at request+0xB0", static_cast<uintptr_t>(sub), 0x100);
        dump("r9 context", reinterpret_cast<uintptr_t>(r9), 0x100);
        uint64_t ctx_ptr = 0;
        if (r9 != nullptr && SafeCopy(reinterpret_cast<uintptr_t>(r9) + 0x28, &ctx_ptr, sizeof(ctx_ptr))) dump("object at r9+0x28", static_cast<uintptr_t>(ctx_ptr), 0x80);
    }
}

void SaveBulletTemplate(const uint8_t* body) {
    eldenring::combat::CombatService::Instance().saveBulletTemplate(body, g_game_dir);
}

void LoadBulletTemplate() {
    eldenring::combat::CombatService::Instance().loadBulletTemplate(g_game_dir);
}

void RecordBulletTemplate(const void* request) {
    eldenring::combat::CombatService::Instance().recordBulletTemplate(request);
}

bool SpawnBulletSafe(void* manager, uint32_t* out_handle, void* request, void* ctx) {
    return eldenring::hooks::GameHooks::Instance().callSpawnBullet(manager, out_handle, request, ctx) != nullptr;
}

void AimCalStart(uint64_t manager, uint32_t handle, const float dir[3]) {
    eldenring::combat::CombatService::Instance().aimCalStart(manager, handle, dir);
}

void AimCalTick() {
    eldenring::combat::CombatService::Instance().aimCalTick();
}

void RunBulletFire() {
    eldenring::combat::CombatService::Instance().runBulletFire(g_ctx);
}

bool QueueBulletFire(float mc_damage) {
    return eldenring::combat::CombatService::Instance().queueBulletFire(mc_damage, g_ctx);
}

bool FireCsShot(float damage, float spread, float range, uint32_t tracer_frequency, uint8_t weapon_slot) {
#if ENABLE_CS2_VIEWMODEL
    eldenring::cs::CsShotParams params;
    params.damage = damage;
    params.spread_deg = spread;
    params.max_range_m = range;
    // Tracers are disabled by design; the weapon registry also keeps tracer_frequency at 0.
    (void)tracer_frequency;
    params.tracer_frequency = 0;
    params.weapon_slot = weapon_slot;
    return eldenring::combat::CombatService::Instance()
        .fireCsShot(params, g_queue, NowTick(), eldenring::RuntimeContext::Instance())
        .fired;
#else
    (void)damage;
    (void)spread;
    (void)range;
    (void)tracer_frequency;
    (void)weapon_slot;
    return false;
#endif
}

// Doomfist strike: 0 hand cannon, 1 rocket punch, 2 uppercut, 3 slam. Runs on
// the input thread and enqueues the damage for the game-thread drain.
bool FireOwStrike(int kind) {
#if ENABLE_OW_VIEWMODEL
    if (kind < 0 || kind > 3) return false;
    // `.hit` (not `.fired`) drives the dispatcher's hitstop: a whiff should not
    // freeze the viewmodel.
    return eldenring::combat::CombatService::Instance()
        .fireOwStrike(static_cast<eldenring::ow::OwStrikeKind>(kind), g_queue, NowTick(),
                      eldenring::RuntimeContext::Instance())
        .hit;
#else
    (void)kind;
    return false;
#endif
}

void SetupBulletLog() {
    const bool proj_log = FileExists(g_game_dir + "mc_er_bulletlog.txt");
    const bool arrow_dmg = FileExists(g_game_dir + "mc_er_arrowdmg.txt");
    const bool fire_enabled = FileExists(g_game_dir + "mc_er_bulletfire.txt");
    const bool cs_shot_log = FileExists(g_game_dir + "mc_er_csshotlog.txt");
    g_proj_log.store(proj_log);
    g_arrow_damage.store(arrow_dmg);
    g_bullet_fire_enabled.store(fire_enabled);
    eldenring::combat::CombatService::Instance().setAimCalEnabled(FileExists(g_game_dir + "mc_er_aimcal.txt"));
    eldenring::combat::CombatService::Instance().setArrowDamage(arrow_dmg);
    eldenring::combat::CombatService::Instance().setBulletFireEnabled(fire_enabled);
    eldenring::combat::CombatService::Instance().setCsShotLogEnabled(cs_shot_log);
    if (cs_shot_log) Log("cs: mc_er_csshotlog.txt: every CS hitscan shot is logged");
    if (arrow_dmg) Log("bullet: mc_er_arrowdmg.txt: the player's bolts do MC bolt damage (9, balanced like the diamond sword)");
    if (fire_enabled) LoadBulletTemplate();
    if (!fire_enabled && !proj_log) return;
    uintptr_t target = 0;
    if (!EnsureMinHook()) {
        Log("bullet: MH_Initialize failed, the request logger is not installed");
    } else if (!LocateByPrefix(g_img, sigs::kSpawnBullet, target)) {
        Log("bullet: spawn_bullet signature not unique, the request logger is not installed");
    } else if (!eldenring::hooks::GameHooks::Instance().setupBulletLog(g_img.base, target)) {
        Log("bullet: hooking spawn_bullet at %p failed", reinterpret_cast<void*>(target));
    } else {
        Log("bullet: spawn_bullet request logger at %p (RVA 0x%llX): the first 40 requests are written here%s", reinterpret_cast<void*>(target),
            static_cast<unsigned long long>(target - g_img.base),
            fire_enabled ? "; mc_er_bulletfire.txt: F3 shoots one bolt of ours from the saved template (a real crossbow shot creates or updates it)" : " (read-only)");
    }
}

} // namespace eldenring::loader_detail
