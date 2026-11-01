#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

void SetupLocomotion() {
    eldenring::movement::LocomotionCallbacks cb;
    cb.get_player_chr = PlayerChrPtr;
    cb.get_physics_module = [](uintptr_t chr) { return detail::readPhysicsModule(g_reader, g_img.base, chr); };
    cb.safe_read = [](uintptr_t addr, void* dst, size_t sz) { return SafeCopy(addr, dst, sz); };
    cb.safe_write = [](uintptr_t addr, const void* src, size_t sz) { return WriteBytesSafe(addr, src, sz); };
    cb.is_game_in_foreground = GameInForeground;
    cb.want_suppress = WantSuppress;
    cb.is_player_dead = PlayerDead;
    cb.is_player_airborne = PlayerAirborne;
    cb.read_camera = [](CameraPose& out_cam) {
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        return world != 0 && readCamera(g_reader, g_img.base, world, out_cam);
    };
    cb.cast_ray = [](const float from[3], const float disp[3]) { return CastStaticRay(from, disp); };
    cb.is_supported_by_block = [](const float pos[3]) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        return g_blocks.count() != 0 && blocks::supportedByBlock(g_blocks, pos);
    };
    cb.resolve_block = [](const float pos[3]) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (g_blocks.count() != 0) return blocks::resolvePlayer(g_blocks, pos);
        blocks::Resolve r;
        r.moved = false;
        r.feet[0] = pos[0]; r.feet[1] = pos[1]; r.feet[2] = pos[2];
        return r;
    };
    cb.resolve_block_swept = [](const float prev[3], const float curr[3]) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (g_blocks.count() != 0) return blocks::resolvePlayerSwept(g_blocks, prev, curr);
        blocks::Resolve r;
        r.moved = false;
        r.feet[0] = curr[0]; r.feet[1] = curr[1]; r.feet[2] = curr[2];
        return r;
    };
    cb.clamp_sneak = [](const float feet[3], float dx, float dz) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        return blocks::clampSneakEdgeMovement(g_blocks, feet, dx, dz);
    };
    cb.check_sprint_collision = [](float target_x, float feet_y, float target_z) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        const auto c_low = blocks::cellOf(target_x, feet_y + 0.1f, target_z);
        const auto c_high = blocks::cellOf(target_x, feet_y + 1.1f, target_z);
        return (g_blocks.get(c_low) != mc::BlockId::Air || g_blocks.get(c_high) != mc::BlockId::Air);
    };
    cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    eldenring::movement::PlayerLocomotion::Instance().setCallbacks(cb);
}

void SetupWorldRuntime() {
    eldenring::world::WorldRuntimeCallbacks cb;
    cb.get_player_chr = PlayerChrPtr;
    cb.get_physics_module = [](uintptr_t chr) { return detail::readPhysicsModule(g_reader, g_img.base, chr); };
    cb.read_physics_pos = [](uintptr_t chr, float out_pos[3]) { return detail::readPhysicsPosition(g_reader, g_img.base, chr, out_pos); };
    cb.safe_read = [](uintptr_t addr, void* dst, size_t sz) { return SafeCopy(addr, dst, sz); };
    cb.safe_write = [](uintptr_t addr, const void* src, size_t sz) { return WriteBytesSafe(addr, src, sz); };
    cb.is_game_in_foreground = GameInForeground;
    cb.read_camera = [](CameraPose& out_cam) {
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        return world != 0 && readCamera(g_reader, g_img.base, world, out_cam);
    };
    cb.cast_ray = [](const float from[3], const float disp[3]) { return CastStaticRay(from, disp); };
    cb.enumerate_enemies = [](std::vector<EnemyInfo>& out) { return enumerateEnemies(g_reader, g_img.base, out, 512); };
    cb.apply_damage_to_chr = [](uintptr_t chr, int dmg) {
        g_queue.enqueue(chr, dmg, NowTick(), 4u | (static_cast<uint32_t>(std::min(255, dmg / 10)) << 8));
    };
    cb.play_sound = [](const char* snd, float vol) { eraudio::Play(snd, vol); };
    cb.spawn_fx = [](int kind, const float pos[3], int count) { erov::SpawnFx(static_cast<erov::FxKind>(kind), pos, count); };
    cb.drive_velocity = [](uintptr_t module, bool pinned, eldenring::RuntimeContext& ctx) {
        eldenring::movement::PlayerLocomotion::Instance().driveVelocity(module, pinned, ctx);
    };
    cb.reset_fall_timer = [](uintptr_t chr) {
        eldenring::movement::PlayerLocomotion::Instance().resetFallTimer(chr);
    };
    cb.unlock_air_movement = [](uintptr_t chr) {
        eldenring::movement::PlayerLocomotion::Instance().unlockAirMovement(chr);
    };
    cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    cb.on_origin_rebase = [](float rx, float ry, float rz) {
        {
            std::lock_guard<std::mutex> g(g_blocks_mutex);
            g_tnt_mgr.shift(rx, ry, rz);
        }
        g_virtual_summons.shift(rx, ry, rz);
        SendBlockMesh();
    };
    eldenring::world::VoxelWorldRuntime::Instance().setCallbacks(cb);

    g_virtual_summons.setGroundSampler([](float x, float y, float z) -> std::optional<float> {
        float feet[3] = {x, y, z};
        const auto cast = [](const float from[3], const float disp[3]) {
            return CastStaticRay(from, disp);
        };
        const auto block_top = [](float bx, float by, float bz, float max_drop) -> std::optional<float> {
            std::lock_guard<std::mutex> g(g_blocks_mutex);
            if (g_blocks.count() == 0) return std::nullopt;
            const int top_cell = static_cast<int>(std::floor(by + 0.06f));
            const int low_cell = static_cast<int>(std::floor(by - max_drop));
            for (int cy = top_cell; cy >= low_cell; --cy) {
                if (g_blocks.get(blocks::cellOf(bx, static_cast<float>(cy), bz)) != mc::BlockId::Air) {
                    return static_cast<float>(cy + 1);
                }
            }
            return std::nullopt;
        };
        return eldenring::shadow::groundBelow(cast, block_top, feet);
    });
}

void SetupCombat() {
    eldenring::combat::CombatCallbacks cb;
    cb.get_player_chr = PlayerChrPtr;
    cb.read_physics_pos = [](uintptr_t chr, float feet[3]) { return detail::readPhysicsPosition(g_reader, g_img.base, chr, feet); };
    cb.read_physics_orient = [](uintptr_t chr, float quat[4]) { return detail::readPhysicsOrientation(g_reader, g_img.base, chr, quat); };
    cb.read_camera = [](CameraPose& out_cam) {
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        return world != 0 && readCamera(g_reader, g_img.base, world, out_cam);
    };
    cb.safe_read = [](uintptr_t addr, void* dst, size_t sz) { return SafeCopy(addr, dst, sz); };
    cb.safe_write = [](uintptr_t addr, const void* src, size_t sz) { return WriteBytesSafe(addr, src, sz); };
    cb.has_line_of_sight = PlayerCanSee;
    cb.read_vitals = [](uintptr_t chr, Vitals& out_v) { return readVitals(g_reader, g_img.base, chr, out_v); };
    cb.set_chr_hp = [](uintptr_t chr, int new_hp) { SetChrHp(chr, new_hp); };
    cb.call_vfunc7 = [](uintptr_t dmg_module, uintptr_t attacker, void* ctx_ptr) { return CallVfunc7(dmg_module, attacker, ctx_ptr); };
    cb.play_sound = [](const char* snd, float vol) { eraudio::Play(snd, vol); };
    cb.spawn_fx = [](int kind, const float pos[3], int count) { erov::SpawnFx(static_cast<erov::FxKind>(kind), pos, count); };
    cb.spawn_tracer = [](const float from[3], const float to[3], float life) { erov::SpawnTracer(from, to, life); };
#if ENABLE_CS2_VIEWMODEL
    cb.spawn_decal = [](const float pos[3], const float normal[3]) { erov::SpawnDecal(pos, normal); };
#endif
    cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    cb.reader = &g_reader;
    cb.image_base = g_img.base;
    cb.get_game_tid = []() -> uint32_t { return g_game_tid.load(std::memory_order_relaxed); };
    cb.spawn_bullet = [](void* manager, uint32_t* out_handle, const void* req, void* status) -> bool {
        return SpawnBulletSafe(manager, out_handle, const_cast<void*>(req), status);
    };
    cb.override_final_damage = [](uint8_t* ctx, uintptr_t atk, uintptr_t vic, int val, int* eng_val) -> bool {
        return overrideFinalDamage(ctx, atk, vic, val, eng_val);
    };
    cb.is_falling = PlayerAirborne;
    cb.file_exists = [](const std::string& path) -> bool {
        return FileExists(g_game_dir + path);
    };
    eldenring::combat::CombatService::Instance().setCallbacks(cb);
}

void SetupInput() {
    SetupLocomotion();
    SetupWorldRuntime();
    SetupCombat();
    {
        eldenring::ui::InventoryCallbacks cb;
        cb.log = [](const char* msg) { Log("%s", msg); };
        eldenring::ui::InventoryController::Instance().setCallbacks(cb);
    }
    {
        eldenring::input::HotkeyCallbacks cb;
        cb.is_game_in_foreground = []() -> bool {
            const bool fg = GameInForeground();
            static int prev_fg = -1;
            static uint64_t last_fail_log = 0;
            if (static_cast<int>(fg) != prev_fg) {
                prev_fg = static_cast<int>(fg);
                HWND top = GetForegroundWindow();
                char cls[80] = {}, title[120] = {};
                DWORD pid = 0;
                if (top != nullptr) {
                    GetClassNameA(top, cls, sizeof(cls) - 1);
                    GetWindowTextA(top, title, sizeof(title) - 1);
                    GetWindowThreadProcessId(top, &pid);
                }
                Log("focus: the game is %s the foreground window (foreground: hwnd=%p pid=%lu class='%s' title='%s')",
                    fg ? "now" : "no longer", reinterpret_cast<void*>(top),
                    static_cast<unsigned long>(pid), cls, title);
            }
            long hr = 0;
            const unsigned failed = erin::TakeKeyboardFailures(hr);
            const uint64_t t = GetTickCount64();
            if (failed > 0 && t - last_fail_log >= 1000) {
                last_fail_log = t;
                Log("input: %u keyboard reads failed in the last second, last error 0x%08lX",
                    failed, static_cast<unsigned long>(hr));
            }
            return fg;
        };
        cb.want_suppress = WantSuppress;
        cb.is_player_dead = PlayerDead;
        cb.is_player_airborne = PlayerAirborne;
        cb.get_player_chr = PlayerChrPtr;
        cb.read_physics_pos = [](uintptr_t chr, float out_pos[3]) {
            return detail::readPhysicsPosition(g_reader, g_img.base, chr, out_pos);
        };
        cb.play_sound = [](const char* s, float v) { eraudio::Play(s, v); };
        cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
        cb.clip_duration = [](const char* n) { return erov::CsClipDuration(n); };
        cb.ow_clip_duration = [](const char* n) { return erov::OwClipDuration(n); };
        cb.take_left_click = erin::TakeLeftClick;
        cb.take_right_click = erin::TakeRightClick;
        cb.left_held = erin::LeftHeld;
        cb.right_held = erin::RightHeld;
        cb.take_wheel_notches = erin::TakeWheelNotches;
        cb.set_suppress_mouse_buttons = erin::SetSuppressMouseButtons;
        cb.set_suppress_mouse_wheel = erin::SetSuppressMouseWheel;
        cb.set_suppress_mouse_motion = erin::SetSuppressMouseMotion;
        cb.set_suppress_keyboard = erin::SetSuppressKeyboard;
        cb.set_masked_keys = erin::SetMaskedKeys;
        cb.take_keyboard_failures = erin::TakeKeyboardFailures;
        cb.step_mining = [](float dt, bool left_held) { return StepMining(dt, left_held); };
        cb.try_ignite_tnt = TryIgniteTnt;
        cb.try_ignite_creeper = TryIgniteCreeper;
        cb.try_place_block = TryPlaceBlock;
        cb.try_use_spawn_egg = TryUseSpawnEgg;
        cb.click_attack = ClickAttack;
        cb.enqueue_nearest_hostile = EnqueueNearestHostile;
        cb.queue_bullet_fire = [](float dmg) { return QueueBulletFire(dmg); };
        cb.queue_cs_shot = [](float damage, float spread, float range, uint32_t tracer, uint8_t slot) { return FireCsShot(damage, spread, range, tracer, slot); };
        cb.ow_strike = [](int kind) { return FireOwStrike(kind); };
        cb.queue_summon = [](const char* src) { return QueueSummon(src); };
        cb.cycle_hidden_slot = [](int& cur) { CycleHiddenSlot(cur); };
        cb.reset_slot_probe = [](int& cur) { ResetSlotProbe(cur); };
        cb.snapshot_native_parts = SnapshotNativeParts;
        cb.request_frame_trace = [](int f) { erov::RequestFrameTrace(f); };
        cb.request_screenshot = [](const char* p) { erov::RequestScreenshot(p); };
        cb.cycle_depth_candidate = erov::CycleDepthCandidate;
        cb.inventory_tick = InventoryTick;
        cb.blocks_tick = []() {
            BlocksTick();
            if (g_mc_mode.load()) LogGroundBytesOnChange();
            if (g_mc_mode.load() && FileExists(g_game_dir + "mc_er_physlog.txt")) LogPhysicsVectors();
        };
        cb.mc_flags_step = McFlagsStep;
        cb.check_summon_watch = [](uint64_t) {
            SummonWatch& summon_watch = g_summon_watch;
            if (summon_watch.reports_left > 0) {
                const uint64_t waited = GetTickCount64() - summon_watch.t0_ms;
                if (waited >= (summon_watch.reports_left == 2 ? 1500u : 5000u)) {
                    --summon_watch.reports_left;
                    std::vector<EnemyInfo> now_list;
                    if (enumerateEnemies(g_reader, g_img.base, now_list, 4000)) {
                        std::vector<uintptr_t> ptrs;
                        for (const EnemyInfo& e : now_list) ptrs.push_back(e.chr);
                        const auto fresh = eldenring::buddy::newEntities(summon_watch.baseline, ptrs);
                        Log("summon: %.1f s after F2, %zu new entities (of %zu)", static_cast<double>(waited) / 1000.0, fresh.size(), now_list.size());
                        for (size_t i : fresh) {
                            const EnemyInfo& e = now_list[i];
                            Log("summon:   new chr=%p npc=%d team=%u hp=%d/%d hostile=%d rel=(%.1f %.1f %.1f) dist=%.1f m", reinterpret_cast<void*>(e.chr),
                                e.npc_id, static_cast<unsigned>(e.team), e.hp, e.max_hp, e.hostile ? 1 : 0, e.rel_x, e.rel_y, e.rel_z,
                                std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z));
                            if (summon_watch.reports_left == 0 && FileExists(g_game_dir + "mc_er_chrscan.txt")) LogChrScan(e.chr, i);
                        }
                    } else {
                        Log("summon: entity enumeration failed");
                    }
                }
            }
        };
        eldenring::input::HotkeyDispatcher::Instance().setCallbacks(cb);
        Log("hotkey_dispatcher: initialized and bound to native game subsystems");
    }
    if (!FileExists(g_game_dir + "mc_er_input.txt")) {
        Log("input: disabled (no mc_er_input.txt)");
        return;
    }
    g_input_enabled.store(true);
    g_click_attack.store(true);
    Log("input: mouse buttons are cleared at the DirectInput layer while MC mode is on and no game UI is open; left click attacks");
}

} // namespace eldenring::loader_detail
