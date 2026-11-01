#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

static constexpr uint64_t kSummonCooldownMs = 200;

void HideSummonChr(uintptr_t chr) {
    if (chr == 0) return;
    for (const uintptr_t at : collectChrDispFlagAddresses(g_reader, g_img.base, chr)) {
        uint32_t flags = 0;
        if (SafeCopy(at, &flags, sizeof(flags)) && needsHiding(flags)) {
            const uint32_t hidden = hideDrawnBit(flags);
            if (WriteBytesSafe(at, &hidden, sizeof(hidden))) {
                g_hidden_flag_addresses.insert(at);
            }
        }
    }

    // Suppress all character SFX slots (ChrConditionSfxSeSlot, ChrMultiSfxSlot, ChrSingleSfxSlot)
    // to eliminate spirit particles, auras and attached audio emitters
    for (const uintptr_t slot : collectChrSfxSlots(g_reader, g_img.base, chr)) {
        uint16_t ctrl = 0;
        const uintptr_t ctrl_addr = slot + eldenring::live::summon::kSlotCtrlWord;
        if (SafeCopy(ctrl_addr, &ctrl, sizeof(ctrl)) && needsSfxSuppression(ctrl)) {
            g_suppressed_sfx_slots[ctrl_addr] = ctrl;
            const uint16_t suppressed = suppressSfxSlotFlags(ctrl);
            WriteBytesSafe(ctrl_addr, &suppressed, sizeof(suppressed));
        }

        // Mask visibility flag on any active particle emitter (CSFD4SfxIns) to hide existing particles
        // safely without invoking mutating intrusive destructor methods cross-thread!
        uintptr_t sfx_ins = 0;
        if (SafeCopy(slot + eldenring::live::summon::kSlotActiveSfx, &sfx_ins, sizeof(sfx_ins)) &&
            sfx_ins >= 0x10000 && sfx_ins < 0x00007FFFFFFEFFFF && (sfx_ins & 7) == 0) {
            uint32_t sfx_flags = 0;
            const uintptr_t flags_addr = sfx_ins + eldenring::live::summon::kSfxFlagsOffset;
            if (SafeCopy(flags_addr, &sfx_flags, sizeof(sfx_flags)) && (sfx_flags & eldenring::live::summon::kSfxVisibleBit) != 0) {
                const uint32_t hidden_sfx = eldenring::live::summon::hideSfxBitmask(sfx_flags);
                WriteBytesSafe(flags_addr, &hidden_sfx, sizeof(hidden_sfx));
            }
        }
    }
}

void HideSummonModels() {
    if (!g_summon_hide.load(std::memory_order_relaxed)) return;
    const bool mc_on = g_mc_mode.load(std::memory_order_relaxed);
    if (!mc_on && g_hidden_flag_addresses.empty() && g_suppressed_sfx_slots.empty()) {
        std::lock_guard<std::mutex> g(g_summon_mutex);
        g_summon_list.clear();
        return;
    }
    std::vector<EnemyInfo> list;
    std::vector<eldenring::mobs::MobSnapshot> seen;
    if (!enumerateEnemies(g_reader, g_img.base, list, 4000, /*include_dead=*/true)) { // the corpses too: the mob over a dead summon plays its death scene
        std::lock_guard<std::mutex> g(g_summon_mutex);
        g_summon_list.clear();
        return;
    }
    static unsigned logged = 0;
    for (const EnemyInfo& e : list) {
        if (e.team != eldenring::live::summon::kTeam) continue;
        {   // where it stands and which way it faces, for the figure drawn over it
            eldenring::mobs::MobSnapshot snap;
            float q[4];
            snap.id = e.chr;
            snap.hp = e.hp;
            snap.max_hp = e.max_hp;
            snap.scale = 1.0f;
            snap.white_flash = 0.0f;
            if (detail::readPhysicsPosition(g_reader, g_img.base, e.chr, snap.feet) && detail::readPhysicsOrientation(g_reader, g_img.base, e.chr, q)) {
                snap.yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]) + g_mob_yaw_offset;
                seen.push_back(snap);
            }
        }
        if (mc_on) {
            HideSummonChr(e.chr);
        } else {
            // Restore model display flags when MC mode is turned off
            for (const uintptr_t at : collectChrDispFlagAddresses(g_reader, g_img.base, e.chr)) {
                uint32_t flags = 0;
                if (!SafeCopy(at, &flags, sizeof(flags))) continue;
                if (g_hidden_flag_addresses.count(at) != 0 && !needsHiding(flags)) {
                    const uint32_t shown = showDrawnBit(flags);
                    if (WriteBytesSafe(at, &shown, sizeof(shown)) && ++logged <= 24) Log("summon hide: MC mode off, restored flags at %p 0x%X -> 0x%X", reinterpret_cast<void*>(at), flags, shown);
                }
            }
            // Restore Sfx slot control words
            for (const uintptr_t slot : collectChrSfxSlots(g_reader, g_img.base, e.chr)) {
                const uintptr_t ctrl_addr = slot + eldenring::live::summon::kSlotCtrlWord;
                const auto it = g_suppressed_sfx_slots.find(ctrl_addr);
                if (it != g_suppressed_sfx_slots.end()) {
                    const uint16_t orig_ctrl = it->second;
                    WriteBytesSafe(ctrl_addr, &orig_ctrl, sizeof(orig_ctrl));
                }
            }
        }
    }

    {   // Diagnostic: what a summon's hit points do when it is hit and when it dies, and how it leaves (does it stay at 0 hp for a while, or just vanish?)
        static std::map<uintptr_t, int> last_hp;
        static unsigned traced = 0;
        for (const auto& snap : seen) {
            const auto it = last_hp.find(snap.id);
            if (it == last_hp.end()) {
                if (++traced <= 80) Log("mob: chr=%p appeared, hp %d/%d", reinterpret_cast<void*>(snap.id), snap.hp, snap.max_hp);
            } else if (it->second != snap.hp && ++traced <= 80) {
                Log("mob: chr=%p hp %d -> %d", reinterpret_cast<void*>(snap.id), it->second, snap.hp);
            }
            last_hp[snap.id] = snap.hp;
        }
        for (auto it = last_hp.begin(); it != last_hp.end();) {
            bool still = false;
            for (const auto& snap : seen) still = still || snap.id == it->first;
            if (still) {
                ++it;
                continue;
            }
            if (++traced <= 80) Log("mob: chr=%p gone, its last hp was %d", reinterpret_cast<void*>(it->first), it->second);
            it = last_hp.erase(it);
        }
    }
    std::vector<eldenring::summon::ArrowDraw> virt_arrows;
    if (!mc_on || PlayerDead()) {
        g_hidden_flag_addresses.clear(); // the ones left belong to summons that are gone: nothing to put back
    } else {
        const auto virt_snaps = g_virtual_summons.generateSnapshots();
        seen.insert(seen.end(), virt_snaps.begin(), virt_snaps.end());
        virt_arrows = g_virtual_summons.getActiveArrows();
    }
    std::lock_guard<std::mutex> g(g_summon_mutex);
    g_summon_list = mc_on ? seen : std::vector<eldenring::mobs::MobSnapshot>{};
    g_summon_arrows = mc_on ? virt_arrows : std::vector<eldenring::summon::ArrowDraw>{};
}

void VirtualSummonsStep(float dt) {
    const bool mc_on = g_mc_mode.load(std::memory_order_relaxed);
    if (!mc_on || PlayerDead()) {
        g_virtual_summons.dismissAll();
        return;
    }
    if (g_virtual_summons.activeCount() == 0) return;

    const uintptr_t player = PlayerChrPtr();
    if (player == 0) return;

    float p_pos[3], p_q[4];
    if (!detail::readPhysicsPosition(g_reader, g_img.base, player, p_pos) ||
        !detail::readPhysicsOrientation(g_reader, g_img.base, player, p_q)) {
        return;
    }

    eldenring::summon::PlayerPose pp;
    pp.pos[0] = p_pos[0];
    pp.pos[1] = p_pos[1];
    pp.pos[2] = p_pos[2];
    pp.yaw = eldenring::render::yawFromQuat(p_q[0], p_q[1], p_q[2], p_q[3]);

    static std::once_flag samplers_init;
    std::call_once(samplers_init, [] {
        g_virtual_summons.setLineOfSightSampler([](const float from[3], const float to[3]) -> bool {
            float disp[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
            const RayResult hit = CastStaticRay(from, disp);
            if (hit.ok && hit.hit && hit.fraction < 0.95f) return false;
            return true;
        });
        g_virtual_summons.setWorldRaycastSampler([](const float from[3], const float to[3], float hit_pos[3]) -> bool {
            float disp[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
            const RayResult hit = CastStaticRay(from, disp);
            if (hit.ok && hit.hit) {
                hit_pos[0] = hit.pos[0];
                hit_pos[1] = hit.pos[1];
                hit_pos[2] = hit.pos[2];
                return true;
            }
            return false;
        });
    });

    std::vector<EnemyInfo> enemies;
    enumerateEnemies(g_reader, g_img.base, enemies, 512);

    std::vector<eldenring::summon::EnemyTarget> enemy_targets;
    for (const EnemyInfo& e : enemies) {
        if (e.team != eldenring::live::summon::kTeam && e.hp > 0 && e.chr != player && e.hostile) {
            eldenring::summon::EnemyTarget et;
            et.chr_ins = e.chr;
            et.hp = e.hp;
            et.max_hp = e.max_hp;
            et.is_hostile = true;
            detail::readPhysicsPosition(g_reader, g_img.base, e.chr, et.pos);
            enemy_targets.push_back(et);
        }
    }

    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    g_virtual_summons.update(dt, pp, enemy_targets);

    auto intents = g_virtual_summons.drainDamageIntents();
    for (const auto& in : intents) {
        g_queue.enqueue(in.target_chr_ins, static_cast<int>(in.damage_amount), NowTick());
        eraudio::Play("entity.arrow.hit", 1.0f);
    }

    auto proj_intents = g_virtual_summons.drainProjectileIntents();
    for (const auto& pi : proj_intents) {
        eraudio::Play("entity.arrow.shoot", 1.0f);
        erov::SpawnFx(erov::FxKind::Crit, pi.origin, 2);
        Log("virtual ranged summon: fired projectile at target %llX (damage=%.1f)",
            static_cast<unsigned long long>(pi.target_chr_ins), pi.damage);
    }

    auto exp_events = g_virtual_summons.drainExplosionEvents();
    for (const auto& ev : exp_events) {
        eraudio::Play("entity.generic.explode", 1.0f);
        Log("virtual creeper: DETONATION at (%.2f %.2f %.2f)!", ev.pos[0], ev.pos[1], ev.pos[2]);
        eldenring::world::VoxelWorldRuntime::Instance().enqueueTntIntent(ev.pos, ev.radius, ev.damage);
    }

    const auto active_arrows = g_virtual_summons.getActiveArrows();
    for (const auto& arr : active_arrows) {
        erov::SpawnFx(erov::FxKind::Crit, arr.pos, 1);
    }
    {
        std::lock_guard<std::mutex> g(g_summon_mutex);
        g_summon_arrows = active_arrows;
    }
}

bool QueueSummon(const char* source, const char* requested_mob) {
    const uint64_t now = GetTickCount64();
    if (now - g_last_summon_ms.load(std::memory_order_relaxed) < kSummonCooldownMs) return false;
    g_last_summon_ms.store(now);

    const uintptr_t player = PlayerChrPtr();
    if (player == 0) {
        Log("%s: refused (no player entity)", source);
        return false;
    }
    float pos[3], q[4];
    if (!detail::readPhysicsPosition(g_reader, g_img.base, player, pos) ||
        !detail::readPhysicsOrientation(g_reader, g_img.base, player, q)) {
        Log("%s: refused (player position/orientation not readable)", source);
        return false;
    }

    eldenring::summon::PlayerPose p;
    p.pos[0] = pos[0];
    p.pos[1] = pos[1];
    p.pos[2] = pos[2];
    p.yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]);

    std::string model_type = (requested_mob != nullptr && requested_mob[0] != '\0') ? requested_mob : "creeper";
    std::vector<uint8_t> summon_txt;
    if (requested_mob == nullptr && ReadFileAll(g_game_dir + "mc_er_summon.txt", summon_txt) && !summon_txt.empty()) {
        std::string content(reinterpret_cast<const char*>(summon_txt.data()), summon_txt.size());
        for (char& c : content) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (content.find("creeper") != std::string::npos) model_type = "creeper";
        else if (content.find("skeleton") != std::string::npos) model_type = "skeleton";
        else if (content.find("iron_golem") != std::string::npos || content.find("golem") != std::string::npos) model_type = "iron_golem";
        else if (content.find("wolf") != std::string::npos) model_type = "wolf";
        else if (content.find("zombie") != std::string::npos) model_type = "zombie";
    }

    uint64_t id = g_virtual_summons.spawnMob(model_type, p, 2.5f);
    Log("%s: virtual summon created (type=%s, id=%llu, active=%zu) at (%.2f, %.2f, %.2f) yaw=%.2f",
        source, model_type.c_str(), id, g_virtual_summons.activeCount(), pos[0], pos[1], pos[2], p.yaw);
    return true;
}

void RunSummonExperiment() {
    // Deprecated: CSBuddyMan spirit ash pipeline is completely replaced by client-managed VirtualSummonManager.
    // Intentionally no-op to eliminate all native Dismiss crashing.
}

} // namespace eldenring::loader_detail
