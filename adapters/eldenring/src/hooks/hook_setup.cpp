#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

void SetupZoneProbe() {
    const bool probe_log = FileExists(g_game_dir + "mc_er_zoneprobe.txt");
    const bool auto_zone = g_loot_zone.load(std::memory_order_relaxed) < 0;
    if (!probe_log && !auto_zone) return;
    if (!EnsureMinHook()) {
        Log("zoneprobe: MH_Initialize failed");
        return;
    }
    uintptr_t collector = 0, material = 0;
    if (!LocateByPrefix(g_img, sigs::kRaycastHitCollector, collector)) {
        Log("zoneprobe: hit collector signature not unique");
        return;
    }
    if (!LocateByPrefix(g_img, sigs::kHavokMaterialLookup, material)) {
        Log("zoneprobe: material lookup signature not unique");
        return;
    }
    g_material_lookup = reinterpret_cast<MaterialLookupFn>(material);
    if (MH_CreateHook(reinterpret_cast<void*>(collector), reinterpret_cast<void*>(&HitCollectorDetour),
                      reinterpret_cast<void**>(&g_hit_collector_orig)) != MH_OK ||
        MH_EnableHook(reinterpret_cast<void*>(collector)) != MH_OK) {
        Log("zoneprobe: hit collector hook failed");
        return;
    }
    g_zone_probe_log.store(probe_log, std::memory_order_relaxed);
    Log("zoneprobe: installed hit collector at %p (RVA 0x%llX), material lookup at %p (RVA 0x%llX), log=%d auto=%d",
        reinterpret_cast<void*>(collector), static_cast<unsigned long long>(collector - g_img.base),
        reinterpret_cast<void*>(material), static_cast<unsigned long long>(material - g_img.base),
        probe_log ? 1 : 0, auto_zone ? 1 : 0);
}

void SetupCreativeProbes() {
    g_cr_log.store(FileExists(g_game_dir + "mc_er_creativelog.txt"));
    uintptr_t kw = 0, fh = 0, fe = 0;
    LocateByPrefix(g_img, sigs::kKillWrapper, kw);
    LocateByPrefix(g_img, sigs::kFallHeight, fh);
    LocateByPrefix(g_img, sigs::kFallTimeExceeded, fe);
    eldenring::hooks::GameHooks::Instance().setupCreativeHooks(
        g_img.base, kw, fh, fe);
    Log("creative: hooks configured (kill_wrapper=%p fall_height=%p fall_time_exceeded=%p)",
        reinterpret_cast<void*>(kw), reinterpret_cast<void*>(fh), reinterpret_cast<void*>(fe));
}

void SetupGameHooksCallbacks() {
    eldenring::hooks::GameHooksCallbacks cb;
    cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    cb.get_game_tid = []() -> uint32_t { return g_game_tid.load(std::memory_order_relaxed); };
    cb.get_player_chr = PlayerChrPtr;
    cb.safe_copy = [](uintptr_t a, void* d, size_t s) { return SafeCopy(a, d, s); };

    cb.on_camera_step_pre = [](float dt) {
        if (g_bullet_fire_pending.load(std::memory_order_relaxed) &&
            GetCurrentThreadId() == g_game_tid.load(std::memory_order_relaxed) &&
            g_bullet_fire_pending.exchange(false)) {
            RunBulletFire();
        }
        FlightStep();
        BlocksCollisionStep();
        McJumpStep();
        GroundSprintStep(dt);
        ShadowGroundStep();
        RestoreCamKeep();
        {
            const uintptr_t p = PlayerChrPtr();
            float p_feet[3] = {};
            if (p != 0 && detail::readPhysicsPosition(g_reader, g_img.base, p, p_feet)) {
                EnemiesBlockCollisionStep(p_feet);
            }
        }
        VirtualSummonsStep(dt);
    };

    cb.check_chr_cam = [](uint64_t self, uintptr_t& addr, float before[3]) -> bool {
        return ChrCamPosAddress(addr) && addr == static_cast<uintptr_t>(self) + layout::kCamMatrix + 0x30 &&
               SafeCopy(addr, before, sizeof(float) * 3);
    };

    cb.on_camera_step_post = [](bool have, uintptr_t addr, const float before[3]) {
        AfterCameraStep(have, addr, before);
    };

    cb.on_clamp_tick = [](uintptr_t module) {
        if (g_damage_enabled.load(std::memory_order_relaxed) && g_queue.pending() > 0) {
            DrainOnce(module);
        }
        if (g_summon_pending.load(std::memory_order_relaxed) && g_summon_pending.exchange(false)) {
            RunSummonExperiment();
        }
        if (g_bullet_fire_pending.load(std::memory_order_relaxed) && g_bullet_fire_pending.exchange(false)) {
            RunBulletFire();
        }
        AimCalTick();
        if (g_heal_pending.load(std::memory_order_relaxed) > 0) {
            uint64_t owner = 0;
            if (SafeCopy(module + layout::kOwnerInDataModule, &owner, sizeof(owner)) && owner != 0 && owner == PlayerChrPtr()) {
                const int h = g_heal_pending.exchange(0);
                if (h > 0) GiveHp(module, h);
            }
        }
    };

    cb.player_hit_survival = [](void* mod, void* atk, uint8_t* ctx, uint8_t a5, uint32_t& heal_to) -> bool {
        if (eldenring::combat::CombatService::Instance().isForcedDamageArmed()) return false;
        return PlayerHitSurvival(mod, atk, ctx, a5, heal_to);
    };

    cb.log_player_hit = [](void* mod, void* atk, uint8_t* ctx, uint8_t a5) {
        if (!eldenring::combat::CombatService::Instance().isForcedDamageArmed() && g_log_player_hits.load(std::memory_order_relaxed)) {
            LogPlayerHit(mod, atk, ctx, a5);
        }
    };

    cb.handle_projectile_hit = [](void* mod, void* atk, uint8_t* ctx) {
        if (!eldenring::combat::CombatService::Instance().isForcedDamageArmed() && (g_proj_log.load(std::memory_order_relaxed) ||
                                g_arrow_damage.load(std::memory_order_relaxed) ||
                                g_bullet_fire_enabled.load(std::memory_order_relaxed))) {
            HandleProjectileHit(mod, atk, ctx);
        }
    };

    cb.handle_forced_damage = [](uint8_t* ctx) {
        eldenring::combat::CombatService::Instance().handleForcedDamage(ctx);
    };

    cb.override_final_damage = [](uint8_t* ctx, uintptr_t atk, uintptr_t vic, int val, int* eng_val) -> bool {
        return overrideFinalDamage(ctx, atk, vic, val, eng_val);
    };

    cb.on_totem_popped_heal = [](uint32_t heal_to) {
        const uintptr_t data = DataModuleOfChr(PlayerChrPtr());
        int32_t hp = 0;
        if (data != 0 && SafeCopy(data + layout::kDataHp, &hp, sizeof(hp)) && static_cast<int32_t>(heal_to) > hp) {
            GiveHp(data, static_cast<int>(heal_to) - hp);
        }
    };

    cb.should_skip_kill_chr = [](void* chr) -> bool {
        if (reinterpret_cast<uintptr_t>(chr) == PlayerChrPtr() && PlayerChrPtr() != 0) {
            const bool recent = GetTickCount64() - g_movement_layer_ms.load(std::memory_order_relaxed) < 3000;
            const bool protect = creative::shouldSkipPlayerKill(g_fall_protect.load(std::memory_order_relaxed),
                                                                g_mc_mode.load(std::memory_order_relaxed),
                                                                BlocksPresent() || recent, PlayerDead() ? 0 : 1);
            const unsigned n = ++g_kills_skipped;
            if (n <= 20) Log("kill: the game kills the player: %s", protect ? "SKIPPED (fall protection)" : "allowed");
            return protect;
        }
        return false;
    };

    cb.should_skip_hit_vfx = [](void* a1, void* a2, uint8_t* ctx, void*) -> bool {
        uint64_t victim = 0, player = 0;
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        const bool known = world != 0 && SafeCopy(reinterpret_cast<uintptr_t>(ctx) + layout::kHitVictim, &victim, sizeof(victim)) &&
                           SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0;
        static std::atomic<unsigned> logged{0};
        if (known && logged.load() < 12 && (victim == player || logged.load() < 4)) {
            ++logged;
            Log("hit vfx: call a1=%p a2=%p ctx victim=%p player=%p -> %s", a1, a2, reinterpret_cast<void*>(victim), reinterpret_cast<void*>(player),
                victim == player ? "PLAYER IS THE VICTIM" : "other victim");
        }
        if (known && victim == player && (g_no_player_hit_vfx.load(std::memory_order_relaxed) || g_first_person.load(std::memory_order_relaxed))) {
            ++g_hitvfx_skipped;
            return true;
        }
        return false;
    };

    cb.should_skip_hit_react = [](void* self, void* out) -> bool {
        if (g_no_stagger.load(std::memory_order_relaxed) && g_mc_mode.load(std::memory_order_relaxed) && IsPlayerActionModule(self)) {
            if (out != nullptr) *static_cast<uint8_t*>(out) = 0;
            if (++g_stagger_skipped <= 10) Log("no_stagger: skipped hit reaction picker for the player");
            return true;
        }
        return false;
    };

    cb.on_render_cam_copy = [](void*, const std::function<void()>& orig) {
        HideSummonModels();
        if (g_native_hide_wanted.load(std::memory_order_relaxed)) {
            const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
            uint64_t player = 0;
            if (world != 0 && SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0) {
                UpdateNativeModel(static_cast<uintptr_t>(player), true);
            }
        }
        uintptr_t pos_addr = 0;
        uintptr_t axes_addr = 0;
        float saved[3] = {};
        float saved_axes[8] = {};
        if (g_first_person.load(std::memory_order_relaxed)) {
            const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
            uint64_t cam = 0, player = 0;
            float feet[3];
            if (world != 0 && SafeCopy(world + layout::kChrCamInWorldChrMan, &cam, sizeof(cam)) && cam != 0 &&
                SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0 &&
                detail::readPhysicsPosition(g_reader, g_img.base, static_cast<uintptr_t>(player), feet) &&
                SafeCopy(static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30, saved, sizeof(saved))) {
                float eye[3];
                firstPersonEye(feet, g_eye_height.load(), eye);
                pos_addr = static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30;
                SafeWrite(pos_addr, eye, sizeof(eye));

                float hurt = 0.f, side = 1.f;
                ReadHurt(hurt, side);
                if (pos_addr != 0 && hurt > 0.f) {
                    const uintptr_t axes = static_cast<uintptr_t>(cam) + layout::kCamMatrix;
                    if (SafeCopy(axes, saved_axes, sizeof(saved_axes))) {
                        const float a = eldenring::fx::hurtTiltRadians(hurt, side), c = std::cos(a), s = std::sin(a);
                        float rolled[8];
                        for (int i = 0; i < 3; ++i) {
                            rolled[i] = saved_axes[i] * c + saved_axes[4 + i] * s;
                            rolled[4 + i] = saved_axes[4 + i] * c - saved_axes[i] * s;
                        }
                        rolled[3] = saved_axes[3];
                        rolled[7] = saved_axes[7];
                        if (SafeWrite(axes, rolled, sizeof(rolled))) {
                            axes_addr = axes;
                        }
                    }
                }
            }
        }
        orig();
        if (pos_addr != 0) SafeWrite(pos_addr, saved, sizeof(saved));
        if (axes_addr != 0) SafeWrite(axes_addr, saved_axes, sizeof(saved_axes));
    };

    cb.on_spawn_bullet = [](const void* request) {
        eldenring::combat::CombatService::Instance().onSpawnBullet(request);
    };

    cb.on_spawn_bullet_post = [](void* manager, void* request, void* r9, uint32_t* out_handle) {
        uint32_t handle = 0xFFFFFFFFu;
        if (out_handle != nullptr) SafeCopy(reinterpret_cast<uintptr_t>(out_handle), &handle, sizeof(handle));
        LogBulletRequest(manager, request, r9, nullptr, handle);
        if (!eldenring::bullet::spawnFailed(handle) && !eldenring::combat::CombatService::Instance().aimCalState().active) {
            uint8_t req[eldenring::bullet::kRequestBytes] = {};
            if (SafeCopy(reinterpret_cast<uintptr_t>(request), req, sizeof(req))) {
                const eldenring::bullet::Fields f = eldenring::bullet::decode(req, sizeof(req));
                if (f.valid) AimCalStart(reinterpret_cast<uint64_t>(manager), handle, f.forward);
            }
        }
    };

    cb.is_fall_damage_disabled = []() -> bool {
        return creative::zeroesFall(CurrentGameMode()) && g_mc_mode.load(std::memory_order_relaxed);
    };

    cb.should_block_creative_kill = [](void* chr) -> bool {
        const uintptr_t player = PlayerChrPtr();
        if (creative::isPlayer(reinterpret_cast<uintptr_t>(chr), player)) {
            const bool block = creative::shouldBlockPlayerKill(CurrentGameMode(), g_mc_mode.load(std::memory_order_relaxed), PlayerDead() ? 0 : 1);
            static std::atomic<unsigned> logged{0};
            if (block || g_cr_log.load(std::memory_order_relaxed)) {
                if (++logged <= 30) {
                    Log("%s%s", creative::formatKill(0, true).c_str(),
                        block ? ": SKIPPED (creative mode, the player still has hit points)" : "");
                }
            }
            return block;
        }
        return false;
    };

    cb.should_deny_fall_time_exceeded = [](void* fall_module, float threshold) -> bool {
        const bool held_up = g_fall_protect.load(std::memory_order_relaxed) &&
                             (BlocksPresent() || GetTickCount64() - g_movement_layer_ms.load(std::memory_order_relaxed) < 3000);
        if (!creative::shouldDenyLongFall(CurrentGameMode(), g_mc_mode.load(std::memory_order_relaxed), creative::isPlayer(ModuleOwner(fall_module), PlayerChrPtr()), held_up)) return false;
        static std::atomic<unsigned> logged{0};
        if (++logged <= 10) Log("creative: the fall lasted %.2f s (threshold %.2f s): answered no for the player", FallTimer(PlayerChrPtr()), static_cast<double>(threshold));
        return true;
    };

    eldenring::hooks::GameHooks::Instance().setCallbacks(cb);
}

void SetupDamage() {
    if (!FileExists(g_game_dir + "mc_er_damage.txt")) {
        Log("damage: disabled (no mc_er_damage.txt)");
        return;
    }
    std::vector<uint8_t> bytes;
    if (!ReadFileAll(g_game_dir + "mc_er_hit.bin", bytes)) {
        Log("damage: mc_er_hit.bin missing, damage disabled");
        return;
    }
    g_template = HitTemplate::fromBytes(bytes.data(), bytes.size());
    if (!g_template) {
        Log("damage: mc_er_hit.bin has %zu bytes, expected %zu; damage disabled", bytes.size(), layout::kHitTemplateSize);
        return;
    }
    uintptr_t clamp = 0;
    if (!LocateByPrefix(g_img, sigs::kClampHp, clamp)) {
        Log("damage: ClampHP signature not unique, damage disabled");
        return;
    }
    if (!EnsureMinHook()) {
        Log("damage: MH_Initialize failed");
        return;
    }

    SetupGameHooksCallbacks();

    uintptr_t pdc = 0;
    if (!LocateByPrefix(g_img, sigs::kProcessDamageContext, pdc)) {
        Log("damage: ProcessDamageContext signature not unique, final damage stays engine-computed");
    } else {
        g_pdc_hooked.store(true);
        Log("damage: ProcessDamageContext hooked at %p (RVA 0x%llX): final damage is replaced for our own hits", reinterpret_cast<void*>(pdc),
            static_cast<unsigned long long>(pdc - g_img.base));
    }
    uintptr_t brk = 0;
    if (FileExists(g_game_dir + "mc_er_noknockdownfix.txt")) {
        g_knockdown_fix.store(false);
        Log("knockdown: mc_er_noknockdownfix.txt present, the player keeps the game's heavy reactions");
    } else {
        if (!LocateByPrefix(g_img, sigs::kBreakTest, brk)) {
            Log("knockdown: break test signature not unique, the player keeps the game's heavy reactions");
        } else {
            Log("knockdown: break test hooked at %p (RVA 0x%llX): in MC mode a hit on the player only flinches", reinterpret_cast<void*>(brk),
                static_cast<unsigned long long>(brk - g_img.base));
        }
    }
    uintptr_t guard = 0, vf7 = 0;
    if (FileExists(g_game_dir + "mc_er_noguardhook.txt")) {
        Log("shield: mc_er_noguardhook.txt present, the engine block path and the knockback fix are off (the damage hook still zeroes blocked hits)");
    } else {
        if (!LocateByPrefix(g_img, sigs::kGuardCheck, guard)) {
            Log("shield: GuardCheck signature not unique, blocked hits are only zeroed in the damage hook");
        } else {
            Log("shield: GuardCheck hooked at %p (RVA 0x%llX): a blocked hit takes the engine's own guard path", reinterpret_cast<void*>(guard),
                static_cast<unsigned long long>(guard - g_img.base));
        }
        if (!LocateByPrefix(g_img, sigs::kDamageVfunc7, vf7)) {
            Log("shield: damage vfunc[7] signature not unique, a blocked hit still pushes the player");
        } else {
            Log("shield: damage vfunc[7] hooked at %p (RVA 0x%llX): a blocked hit does not push the player", reinterpret_cast<void*>(vf7),
                static_cast<unsigned long long>(vf7 - g_img.base));
        }
    }
    uintptr_t rcc = 0;
    if (!LocateByPrefix(g_img, sigs::kRenderCameraCopy, rcc)) {
        Log("first person: render camera copy signature not unique, experiment unavailable");
    } else {
        Log("first person: render camera copy hooked at %p (RVA 0x%llX); first_person=1 or F10 turns the experiment on", reinterpret_cast<void*>(rcc),
            static_cast<unsigned long long>(rcc - g_img.base));
        g_summon_hide.store(!FileExists(g_game_dir + "mc_er_nosummonhide.txt"));
        if (g_summon_hide.load()) Log("summon hide: on: team 47 models, shadows, and spirit particles are cleared before each frame");
    }
    uintptr_t hp_fn = 0;
    if (LocateByPrefix(g_img, sigs::kApplyHpChange, hp_fn)) {
        g_apply_hp = reinterpret_cast<ApplyHpFn>(hp_fn);
        Log("survival: ApplyHPChange at %p (RVA 0x%llX): food, regeneration and the totem heal through it", reinterpret_cast<void*>(hp_fn),
            static_cast<unsigned long long>(hp_fn - g_img.base));
    } else {
        Log("survival: ApplyHPChange signature not unique, no healing (the totem still clamps lethal damage)");
    }
    g_log_player_hits.store(FileExists(g_game_dir + "mc_er_hitlog.txt"));
    if (g_log_player_hits.load()) Log("hitlog: logging the HitContext of every hit the player takes");
    uintptr_t vfx = 0;
    if (FileExists(g_game_dir + "mc_er_hitvfx.txt")) {
        if (!LocateByPrefix(g_img, sigs::kHitVfxSpawn, vfx)) {
            Log("hit vfx: spawner signature not unique, the player's blood effect cannot be removed");
        } else {
            Log("hit vfx: spawner hooked at %p (RVA 0x%llX); no_player_hit_vfx=1 or first person removes the player's hit effect",
                reinterpret_cast<void*>(vfx), static_cast<unsigned long long>(vfx - g_img.base));
        }
    }
    uintptr_t step = 0;
    if (FileExists(g_game_dir + "mc_er_camwatch.txt")) {
        Log("camwatch: diagnostic on (mc_er_camwatch.txt): the camera update hook is not installed, writers of ChrCam position are logged");
        CreateThread(nullptr, 0, CamWatchThread, nullptr, 0, nullptr);
    } else if (!LocateByPrefix(g_img, sigs::kCameraStepExecute, step)) {
        Log("camstep: camera update signature not unique, hit effects stay at the third-person position in first person");
    } else {
        g_camstep_installed.store(true);
        Log("camstep: camera update hooked at %p (RVA 0x%llX): first person keeps the eye position for the whole frame (fp_persist=0 turns it off)",
            reinterpret_cast<void*>(step), static_cast<unsigned long long>(step - g_img.base));
    }
    uintptr_t gate = 0, freeze = 0;
    if (!LocateByPrefix(g_img, sigs::kIsInputBlocked, gate)) {
        Log("inventory: input gate signature not unique, the character keeps moving while the inventory is open");
    } else {
        Log("inventory: input gate hooked at %p (RVA 0x%llX)", reinterpret_cast<void*>(gate), static_cast<unsigned long long>(gate - g_img.base));
    }
    if (!LocateByPrefix(g_img, sigs::kMenuFreezesCamera, freeze)) {
        Log("inventory: camera freeze signature not unique, the camera keeps turning while the inventory is open");
    } else {
        Log("inventory: camera freeze hooked at %p (RVA 0x%llX)", reinterpret_cast<void*>(freeze), static_cast<unsigned long long>(freeze - g_img.base));
    }
    uintptr_t react = 0, heavy = 0;
    if (!LocateByPrefix(g_img, sigs::kHitReactDefault, react) || !LocateByPrefix(g_img, sigs::kHitReactHeavy, heavy)) {
        Log("no_stagger: hit reaction signatures not unique, the experiment is unavailable");
    } else {
        Log("no_stagger: reaction pickers hooked at %p / %p (RVA 0x%llX / 0x%llX); no_stagger=1 in mc_er_steve.txt turns the experiment on",
            reinterpret_cast<void*>(react), reinterpret_cast<void*>(heavy), static_cast<unsigned long long>(react - g_img.base),
            static_cast<unsigned long long>(heavy - g_img.base));
    }
    uintptr_t kill = 0;
    if (!LocateByPrefix(g_img, sigs::kKillChr, kill)) {
        Log("kill: signature not unique, fall protection is unavailable");
    } else {
        Log("kill: the kill-character function is hooked at %p (RVA 0x%llX); while blocks exist or our jump is recent the player's kill is skipped", reinterpret_cast<void*>(kill),
            static_cast<unsigned long long>(kill - g_img.base));
    }
    if (FileExists(g_game_dir + "mc_er_nolos.txt")) {
        Log("los: disabled by mc_er_nolos.txt");
    } else {
        uintptr_t ray = 0;
        if (!LocateByPrefix(g_img, sigs::kRaycastWrapper, ray)) {
            Log("los: raycast wrapper signature not unique, walls do not block hits");
        } else {
            eldenring::combat::CombatService::Instance().setRaycastFn(reinterpret_cast<RaycastFn>(ray));
            g_los_enabled.store(true);
            Log("los: raycast wrapper at %p (RVA 0x%llX), filter 0x%X, walls block hits", reinterpret_cast<void*>(ray),
                static_cast<unsigned long long>(ray - g_img.base), los::kStaticGeometryFilter);
        }
    }
    g_require_victim_updating.store(FileExists(g_game_dir + "mc_er_require_victim.txt"));
    g_damage_enabled.store(true);
    Log("damage: enabled; ClampHP hooked at %p (RVA 0x%llX); press F8 to hit the nearest hostile enemy (require_victim_updating=%d)", reinterpret_cast<void*>(clamp),
        static_cast<unsigned long long>(clamp - g_img.base), g_require_victim_updating.load() ? 1 : 0);

    eldenring::hooks::GameHooks::Instance().setupDamageHooks(
        g_img.base, clamp, pdc, brk, guard, vf7, rcc, vfx, step, gate, freeze, react, heavy, kill);
}

} // namespace eldenring::loader_detail
