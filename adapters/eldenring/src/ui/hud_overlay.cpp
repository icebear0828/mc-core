#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

void RestoreNativeModel() {
    eldenring::ui::RestoreNativeModel(g_hidden_flags, g_native_mutex);
}

void UpdateNativeModel(uintptr_t player, bool hide) {
    eldenring::ui::UpdateNativeModel(player, hide, g_reader, g_img.base, g_ctx, g_hidden_flags, g_native_mutex);
}

bool HudProvider(erov::HudState& out, erov::SteveState& steve) {
    {   // the mobs over the summons (mc_er_summonhide.txt): only while MC mode is on, otherwise the wolves are not drawn at all and nothing replaces them
        std::vector<eldenring::mobs::MobSnapshot> mobs;
        std::vector<eldenring::summon::ArrowDraw> arrows;
        if (g_summon_hide.load(std::memory_order_relaxed) && g_mc_mode.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> g(g_summon_mutex);
            mobs = g_summon_list;
            arrows = g_summon_arrows;
        }
        erov::SetMobs(mobs);
        erov::SetArrows(arrows);
    }
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    if (world == 0) return false;
    uint64_t player = 0;
    Vitals v;
    if (!SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0 ||
        !readVitals(g_reader, g_img.base, static_cast<uintptr_t>(player), v)) {
        return false;
    }
    MenuState ms;
    LoadingState ls;
    float fade = 0.f;
    const bool have_state = readMenuState(g_reader, readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan), ms) &&
                            readLoadingState(g_reader, readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper), ls) &&
                            readFadeAlpha(g_reader, readSingleton(g_reader, g_img.base, g_rva_fade, sigs::kCSFade), fade);

    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        eldenring::ui::PopulateHudStateFromContext(out, g_ctx);
        static int last_logged_armor_pts = -1;
        if (out.armor_points != last_logged_armor_pts) {
            Log("ARMOR-HUD: total_armor_points=%d (reduction=%.1f%%, inv_open=%d)",
                out.armor_points, std::min(80.f, out.armor_points * 4.0f), g_inv_open.load());
            last_logged_armor_pts = out.armor_points;
        }
    }

    out.hp = v.hp;
    out.max_hp = v.max_hp;
    out.show = have_state && !ms.menu_focused && !ms.popup_open && !ls.screen_loading && fade < 0.02f;

    const bool ow_fp = eldenring::RuntimeContext::Instance().mode().ow_mode.load(std::memory_order_relaxed) &&
                       g_first_person.load(std::memory_order_relaxed);
    if (g_hide_native.load() || out.mc_mode || out.cs_mode || ow_fp) {
        // Hide the native body in MC, CS, and OW first-person modes. The collector in
        // hud_bridge.cpp is RTTI-validated, so a weapon switch that leaves a
        // stale part pointer cannot turn a display-flag write into a vtable
        // overwrite (the 0x200000001 crash).
        const bool want_hidden = (out.mc_mode || out.cs_mode || ow_fp) && have_state && !ls.screen_loading;
        g_native_hide_wanted.store(want_hidden);
        UpdateNativeModel(static_cast<uintptr_t>(player), want_hidden);
        static uint64_t last_report_ms = 0;
        const uint64_t now_ms = GetTickCount64();
        if (now_ms - last_report_ms >= 1000) {
            last_report_ms = now_ms;
            const unsigned n = g_native_reshown.exchange(0);
            if (n != 0) Log("native: the game turned hidden parts back on %u time(s) in the last second", n);
            const unsigned sk = g_hitvfx_skipped.exchange(0);
            if (sk != 0) Log("hit vfx: skipped the player's hit effect %u time(s)", sk);
        }
    }

    steve.draw = false;
    steve.cam_valid = false;
    CameraPose cam;
    float feet[3], q[4];
    const bool ow_view = eldenring::RuntimeContext::Instance().mode().ow_mode.load(std::memory_order_relaxed);
    if (g_steve_enabled.load() && readCamera(g_reader, g_img.base, world, cam) &&
        detail::readPhysicsPosition(g_reader, g_img.base, static_cast<uintptr_t>(player), feet) &&
        detail::readPhysicsOrientation(g_reader, g_img.base, static_cast<uintptr_t>(player), q)) {
        // OW hides Steve, but the combat particles (hit sparks, damage
        // numbers) still need a valid world camera to project into.
        steve.draw = !ow_view;
        steve.cam_valid = true;
        steve.cam.right = {cam.right[0], cam.right[1], cam.right[2]};
        steve.cam.up = {cam.up[0], cam.up[1], cam.up[2]};
        steve.cam.forward = {cam.forward[0], cam.forward[1], cam.forward[2]};
        if (g_first_person.load(std::memory_order_relaxed)) {
            steve.cam.position = {feet[0], feet[1] + g_eye_height.load(std::memory_order_relaxed), feet[2]};
        } else {
            steve.cam.position = {cam.position[0], cam.position[1], cam.position[2]};
        }
        steve.fov_y = cam.fov_y * g_sprint_fov.load(std::memory_order_relaxed);
        steve.feet[0] = feet[0];
        steve.feet[1] = feet[1];
        steve.feet[2] = feet[2];
        steve.yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]) + g_steve_yaw_offset;
        if (g_flying.load(std::memory_order_relaxed)) steve.yaw = g_fly_yaw.load(std::memory_order_relaxed); // the game does not turn a character that never sees a key
        float grace_dist = 999.f;
        const bool near_grace = findNearestGrace(g_reader, g_img.base, world, feet, 2.8f, &grace_dist);
        steve.sleeping = near_grace && (ms.menu_focused || fade >= 0.02f);
    }
    const bool steve_dead_now = v.max_hp > 0 && v.hp <= 0;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        eldenring::ui::PopulateSteveStateFromContext(steve, g_ctx, steve_dead_now, steve.sleeping);
    }
    {
        static bool was_dead = false;
        if (steve.dead && !was_dead && g_mc_mode.load(std::memory_order_relaxed)) eraudio::Play("entity.player.death");
        was_dead = steve.dead;
    }
    if (steve.sleeping) {
        steve.first_person = false;
        steve.draw = true; // Always draw Steve when sleeping at a grace
    } else if (g_slot_probe.load() >= 0 || g_first_person.load()) {
        steve.draw = false; // slot probing: show only the native model, with the one slot missing
    }
    {   // why Steve is or is not drawn, logged when any of the inputs changes
        struct Seen { bool draw, enabled, first_person, show, mc, cam_ok; int slot_probe; bool valid; };
        static Seen last{};
        const Seen now{steve.draw, g_steve_enabled.load(), g_first_person.load(), out.show, out.mc_mode, steve.cam_valid, g_slot_probe.load(), true};
        if (!last.valid || now.draw != last.draw || now.enabled != last.enabled || now.first_person != last.first_person || now.show != last.show ||
            now.mc != last.mc || now.cam_ok != last.cam_ok || now.slot_probe != last.slot_probe) {
            Log("STEVE-STATE: draw=%d enabled=%d cam_valid=%d first_person=%d slot_probe=%d show=%d mc_mode=%d dead=%d", now.draw ? 1 : 0, now.enabled ? 1 : 0,
                now.cam_ok ? 1 : 0, now.first_person ? 1 : 0, now.slot_probe, now.show ? 1 : 0, now.mc ? 1 : 0, steve.dead ? 1 : 0);
            last = now;
        }
    }
    {
        const auto& dbg = eldenring::RuntimeContext::Instance().mode();
        steve.gliding = dbg.dbg_glide.load(std::memory_order_relaxed);
        steve.riptide = dbg.dbg_riptide.load(std::memory_order_relaxed);
        steve.trident_charge = dbg.trident_charge.load(std::memory_order_relaxed);
        steve.firework_seq = dbg.dbg_firework_seq.load(std::memory_order_relaxed);
    }
    return true;
}

void SetupOverlay() {
    if (!FileExists(g_game_dir + "mc_er_overlay.txt")) {
        Log("overlay: disabled (no mc_er_overlay.txt)");
        return;
    }
    if (!EnsureMinHook()) {
        Log("overlay: MH_Initialize failed");
        return;
    }
    if (FileExists(g_game_dir + "mc_er_steve.txt")) {
        erov::SteveConfig cfg;
        std::vector<uint8_t> bytes;
        if (ReadFileAll(g_game_dir + "mc_er_steve.txt", bytes)) {
            const std::string text(bytes.begin(), bytes.end());
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos) end = text.size();
                const std::string line = text.substr(pos, end - pos);
                pos = end + 1;
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq);
                const float value = static_cast<float>(atof(line.c_str() + eq + 1));
                if (key == "occlusion") cfg.occlusion = value != 0.f;
                else if (key == "debug") cfg.debug = static_cast<int>(value);
                else if (key == "depthview_gain") cfg.depthview_gain = value;
                else if (key == "depth_const") cfg.depth_const = value;
                else if (key == "rel_bias") cfg.rel_bias = value;
                else if (key == "abs_bias") cfg.abs_bias = value;
                else if (key == "scene_height") cfg.scene_height = value;
                else if (key == "yaw_offset_deg") g_steve_yaw_offset = value * 3.14159265f / 180.f;
                else if (key == "mob_yaw_offset_deg") g_mob_yaw_offset = value * 3.14159265f / 180.f;
                else if (key == "hide_native") g_hide_native.store(value != 0.f);
                else if (key == "fp_persist") g_fp_persist.store(value != 0.f);
                else if (key == "sound_volume") g_sound_volume.store(value);
                else if (key == "no_player_hit_vfx") g_no_player_hit_vfx.store(value != 0.f);
                else if (key == "first_person") g_first_person.store(value != 0.f);
                else if (key == "shadow") g_shadow_enabled.store(value != 0.f);
                else if (key == "bolt_attach_poly") eldenring::combat::CombatService::Instance().setBoltAttachPoly(static_cast<int>(value));
                else if (key == "eye_height") g_eye_height.store(value);
                else if (key == "fall_reset") g_fall_reset.store(value != 0.f);
                else if (key == "fall_hold") g_fall_hold.store(std::clamp(value, 0.f, 2.8f));
                else if (key == "fall_protect") g_fall_protect.store(value != 0.f);
                else if (key == "fly_sync") g_fly_sync.store(value != 0.f);
                else if (key == "fly_speed") g_fly_h_speed.store(std::clamp(value, 1.f, 40.f));
                else if (key == "fly_vspeed") g_fly_v_speed.store(std::clamp(value, 1.f, 40.f));
                else if (key == "mc_jump") g_mc_jump.store(value != 0.f);
                else if (key == "mc_jump_key") g_mc_jump_vk.store(static_cast<int>(value));
                else if (key == "mc_mode") {
#if ENABLE_MC_MODE
                    g_mc_mode.store(value != 0.f);
#else
                    g_mc_mode.store(false); // built without MC gameplay
#endif
                }
                else if (key == "ow_mode") {
#if ENABLE_OW_VIEWMODEL
                    const bool ow = (value != 0.f);
                    eldenring::RuntimeContext::Instance().mode().ow_mode.store(ow);
                    if (ow) {
                        g_mc_mode.store(false);
                        eldenring::RuntimeContext::Instance().mode().mc_mode.store(false);
                        eldenring::RuntimeContext::Instance().mode().cs_mode.store(false);
                        eldenring::RuntimeContext::Instance().mode().first_person.store(true);
                    }
#endif
                }
                else if (key == "mc_gravity") g_mc_gravity.store(std::clamp(value, 5.f, 80.f));
                else if (key == "mc_jump_speed") g_mc_jump_speed.store(std::clamp(value, 1.f, 20.f));
                else if (key == "blocks") g_blocks_enabled.store(value != 0.f);
                else if (key == "ground_mining") g_ground_mining_enabled.store(value != 0.f);
                else if (key == "ground_crack_lift") g_ground_crack_lift.store(std::clamp(value, -1.0f, 0.5f));
                else if (key == "ground_crack_size") g_ground_crack_size.store(std::clamp(value, 0.2f, 3.0f));
                else if (key == "loot_zone") {
                    const int zone = static_cast<int>(value);
                    g_loot_zone.store(zone < 0 ? -1 : std::clamp(zone, 0, static_cast<int>(mc::LootZone::Count) - 1));
                }
                else if (key.rfind("zone_mat_", 0) == 0) {
                    const int material = atoi(key.c_str() + 9);
                    const int zone = std::clamp(static_cast<int>(value), 0, static_cast<int>(mc::LootZone::Count) - 1);
                    g_zone_material_rules.push_back({static_cast<uint32_t>(material), static_cast<mc::LootZone>(zone)});
                }
                else if (key.rfind("zone_area_", 0) == 0) {
                    const int area = atoi(key.c_str() + 10);
                    const int zone = std::clamp(static_cast<int>(value), 0, static_cast<int>(mc::LootZone::Count) - 1);
                    g_zone_area_rules.push_back({static_cast<uint32_t>(area), static_cast<mc::LootZone>(zone)});
                }
                else if (key == "block_drive") g_block_drive.store(value != 0.f);
                else if (key == "instant_turn") g_instant_turn.store(value != 0.f);
                else if (key == "turn_speed") g_turn_speed.store(std::clamp(value, 0.5f, 50.f));
                else if (key == "block_speed" || key == "walk_speed") g_block_walk_speed.store(std::clamp(value, 0.5f, 15.f));
                else if (key == "sneak_speed") g_sneak_speed.store(std::clamp(value, 0.2f, 10.f));
                else if (key == "sprint_speed") g_sprint_speed.store(std::clamp(value, 0.5f, 20.f));
                else if (key == "sprint_boost") g_sprint_boost.store(std::clamp(value, 0.0f, 20.f));
                else if (key == "sneak_key") g_sneak_vk.store(static_cast<int>(value));
                else if (key == "disable_vanilla_roll" || key == "disable_roll") g_disable_vanilla_roll.store(value != 0.f);
                else if (key == "roll_key") g_roll_vk.store(static_cast<int>(value));
                else if (key == "block_anchor") g_block_anchor.store(value != 0.f);
                else if (key == "block_ground") g_block_ground.store(value != 0.f);
                else if (key == "block_collision") g_block_collision.store(value != 0.f);
                else if (key == "enemy_block_collision" || key == "enemy_collision") g_enemy_block_collision.store(value != 0.f);
                else if (key == "reach") g_reach.store(std::clamp(value, 1.f, 8.f));
                else if (key == "no_stagger") g_no_stagger.store(value != 0.f);
                else if (key == "inv_key") g_inv_vk.store(static_cast<int>(value));
                else if (key == "inv_sens") g_inv_sens.store(std::max(0.1f, value));
                else if (key == "kb_force") g_kb_force.store(value);
                else if (key == "damage_scale") g_damage_scale.store(std::clamp(value, 0.0f, 200.0f));
                else if (key == "damage_hp_percent") g_damage_hp_percent.store(std::clamp(value, 0.0f, 1.0f));
                else if (key == "sweep_radius") g_sweep_radius.store(std::clamp(value, 0.5f, 10.0f));
                else if (key == "hide_slots") g_hide_slots.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
                else if (key == "hide_mask1") g_hide_mask1.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
                else if (key == "hide_mask2") g_hide_mask2.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
            }
            // The vectors stop growing here (the config is read once), so the spans stay valid.
            g_zone_rules.material_rules = g_zone_material_rules;
            g_zone_rules.area_rules = g_zone_area_rules.empty()
                ? DefaultZoneAreaRules()
                : std::span<const mc::ZoneRule>(g_zone_area_rules);
        }
        cfg.depth_probe = FileExists(g_game_dir + "mc_er_depthprobe.txt");
        erov::SetSteveConfig(cfg);
        eraudio::Init((g_game_dir + "mods\\mc_adapter\\sounds\\").c_str(), g_sound_volume.load(), &Log);
        {
            std::vector<uint8_t> atlas;
            unsigned aw = 0, ah = 0;
            const std::string atlas_path_sub = g_game_dir + "mods\\mc_adapter\\mc_hud_atlas.png";
            const std::string atlas_path_root = g_game_dir + "mc_hud_atlas.png";
            const std::string atlas_path = FileExists(atlas_path_sub) ? atlas_path_sub : atlas_path_root;
            if (erov::DecodePngFile(atlas_path, atlas, aw, ah) && aw == mc::hud::kHudAtlasWidth && ah == mc::hud::kHudAtlasHeight) {
                erov::SetHudAtlas(atlas.data(), aw, ah);
                Log("hud atlas: external file %s (%ux%u)", atlas_path.c_str(), aw, ah);
            } else {
                Log("hud atlas: %s missing or not %ux%u (regenerate it with tools/extract_mc_assets.py), the HUD stays plain rectangles", atlas_path.c_str(),
                    static_cast<unsigned>(mc::hud::kHudAtlasWidth), static_cast<unsigned>(mc::hud::kHudAtlasHeight));
            }
        }
        {
            std::vector<uint8_t> skin;
            unsigned sw = 0, sh = 0;
            const std::string skin_path = g_game_dir + "mods\\mc_adapter\\steve.png";
            if (erov::DecodePngFile(skin_path, skin, sw, sh) && sw == 64 && sh == 64) {
                erov::SetSteveSkin(skin.data(), sw, sh);
                Log("skin: external file %s (%ux%u)", skin_path.c_str(), sw, sh);
            } else {
                Log("skin: %s missing or not 64x64, the figure stays flat grey-brown (extract it with tools/extract_mc_assets.py --export-steve-skin)", skin_path.c_str());
            }
        }
        {   // Load all available mob models and skins (creeper, zombie, skeleton)
            const std::vector<std::string> supported_mobs = {"creeper", "zombie", "skeleton"};
            for (const auto& mob : supported_mobs) {
                std::string model_path = g_game_dir + "mods\\mc_adapter\\models\\" + mob + ".geo.json";
                std::vector<uint8_t> json;
                if (ReadFileAll(model_path, json)) {
                    auto model = mc::model::EntityModel::fromJson(std::string_view(reinterpret_cast<const char*>(json.data()), json.size()));
                    if (model && !model->bones.empty()) {
                        erov::SetMobModel(mob, *model);
                        Log("mobs: model %s (%s): %zu bones", mob.c_str(), model->identifier.c_str(), model->bones.size());
                    }
                } else {
                    Log("mobs: model file missing at %s", model_path.c_str());
                }
                std::string skin_path = g_game_dir + "mods\\mc_adapter\\" + mob + ".png";
                std::vector<uint8_t> mob_skin;
                unsigned zw = 0, zh = 0;
                if (erov::DecodePngFile(skin_path, mob_skin, zw, zh) && zw == 64 && (zh == 64 || zh == 32)) {
                    erov::SetMobSkin(mob, mob_skin.data(), zw, zh);
                    Log("mobs: skin %s (%ux%u)", mob.c_str(), zw, zh);
                } else {
                    Log("mobs: skin %s missing or not 64x64/64x32 at %s", mob.c_str(), skin_path.c_str());
                }
            }
        }
        {   // Props: the elytra wings (on the torso while gliding) and the trident (3D in the right hand).
            for (const char* prop : {"elytra_wings", "trident"}) {
                const std::string model_path = g_game_dir + "mods\\mc_adapter\\models\\" + prop + ".geo.json";
                std::vector<uint8_t> json;
                if (!ReadFileAll(model_path, json)) continue;
                auto model = mc::model::EntityModel::fromJson(std::string_view(reinterpret_cast<const char*>(json.data()), json.size()));
                if (model && !model->bones.empty()) {
                    erov::RegisterModel(prop, *model);
                    Log("props: model %s (%zu bones)", prop, model->bones.size());
                }
                // The prop's own texture, when the deploy put one next to the model (the elytra is 64x32).
                const std::string skin_path = g_game_dir + "mods\\mc_adapter\\" + prop + ".png";
                std::vector<uint8_t> prop_skin;
                unsigned pw = 0, ph = 0;
                if (erov::DecodePngFile(skin_path, prop_skin, pw, ph) && pw == 64 && (ph == 32 || ph == 64)) {
                    erov::SetMobSkin(prop, prop_skin.data(), pw, ph);
                    Log("props: skin %s (%ux%u)", prop, pw, ph);
                }
            }
        }
        g_steve_enabled.store(true);
        Log("steve: hide_native=%d mask1=0x%X mask2=0x%X slots=0x%X", g_hide_native.load() ? 1 : 0, g_hide_mask1.load(), g_hide_mask2.load(),
        g_hide_slots.load());
        Log("steve: enabled (occlusion=%d depth_const=%.4f rel_bias=%.3f abs_bias=%.3f scene_height=%.0f yaw_offset=%.1f deg)",
            cfg.occlusion ? 1 : 0, cfg.depth_const, cfg.rel_bias, cfg.abs_bias, cfg.scene_height, g_steve_yaw_offset * 180.f / 3.14159265f);
    }
#if ENABLE_OW_VIEWMODEL
    {
        // OW has its own config file: the gauntlet's FOV, scale and offset are
        // independent from the Steve/CS tuning in mc_er_steve.txt, and load
        // even when that file is absent.
        erov::OwConfig ow_cfg;
        std::vector<uint8_t> ow_bytes;
        if (FileExists(g_game_dir + "mc_er_ow.txt") && ReadFileAll(g_game_dir + "mc_er_ow.txt", ow_bytes)) {
            const std::string text(ow_bytes.begin(), ow_bytes.end());
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos) end = text.size();
                const std::string line = text.substr(pos, end - pos);
                pos = end + 1;
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq);
                const float value = static_cast<float>(atof(line.c_str() + eq + 1));
                if (key == "ow_mode") {
                    const bool ow = (value != 0.f);
                    eldenring::RuntimeContext::Instance().mode().ow_mode.store(ow);
                    if (ow) {
                        g_mc_mode.store(false);
                        eldenring::RuntimeContext::Instance().mode().mc_mode.store(false);
                        eldenring::RuntimeContext::Instance().mode().cs_mode.store(false);
                        eldenring::RuntimeContext::Instance().mode().first_person.store(true);
                    }
                } else if (key == "ow_fov") {
                    ow_cfg.fov_deg = std::clamp(value, 20.f, 140.f);
                } else if (key == "ow_scale") {
                    ow_cfg.scale = std::clamp(value, 0.05f, 10.f);
                } else if (key == "ow_offset_x") {
                    ow_cfg.offset_x = value;
                } else if (key == "ow_offset_y") {
                    ow_cfg.offset_y = value;
                } else if (key == "ow_offset_z") {
                    ow_cfg.offset_z = value;
                } else if (key == "ow_dash_distance") {
                    eldenring::RuntimeContext::Instance().mode().ow_dash_distance.store(std::clamp(value, 0.f, 15.f));
                } else if (key == "ow_damage") {
                    eldenring::RuntimeContext::Instance().mode().ow_damage_enabled.store(value != 0.f);
                } else if (key == "ow_damage_scale") {
                    eldenring::RuntimeContext::Instance().mode().ow_damage_scale.store(std::clamp(value, 0.f, 10.f));
                } else if (key == "ow_shoot_motion") {
                    ow_cfg.shoot_motion = std::clamp(value, 0.f, 1.f);
                }
            }
            Log("ow config: %s", (g_game_dir + "mc_er_ow.txt").c_str());
        }
        erov::SetOwConfig(ow_cfg);
    }
#endif
    // Startup mode: tools/deploy_modes.py writes mc_er_mode.txt with
    // `start_mode=mc|cs|ow`. It selects the active mode on launch; hotkeys still
    // switch between every mode this build contains. A deployment that installs
    // only one mode writes that mode, so an OW-only install never starts in CS
    // or MC.
    {
        std::vector<uint8_t> mode_bytes;
        if (FileExists(g_game_dir + "mc_er_mode.txt") && ReadFileAll(g_game_dir + "mc_er_mode.txt", mode_bytes)) {
            const std::string text(mode_bytes.begin(), mode_bytes.end());
            std::string start = "mc";
            const size_t key_pos = text.find("start_mode");
            if (key_pos != std::string::npos) {
                const size_t eq = text.find('=', key_pos);
                if (eq != std::string::npos) {
                    size_t begin = eq + 1;
                    size_t end = text.find_first_of("\r\n", begin);
                    if (end == std::string::npos) end = text.size();
                    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
                    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
                    start = text.substr(begin, end - begin);
                }
            }
            auto& m = eldenring::RuntimeContext::Instance().mode();
            const bool want_mc = start == "mc";
            const bool want_cs = start == "cs";
            const bool want_ow = start == "ow";
            m.mc_mode.store(want_mc && ENABLE_MC_MODE != 0);
            m.cs_mode.store(want_cs && ENABLE_CS2_VIEWMODEL != 0);
            m.ow_mode.store(want_ow && ENABLE_OW_VIEWMODEL != 0);
            g_mc_mode.store(want_mc && ENABLE_MC_MODE != 0);
            if (want_cs || want_ow) m.first_person.store(true);
            Log("start mode: %s (mc=%d cs=%d ow=%d)", start.c_str(), m.mc_mode.load() ? 1 : 0,
                m.cs_mode.load() ? 1 : 0, m.ow_mode.load() ? 1 : 0);
        }
    }
    erov::Install(&HudProvider, [](const char* fmt, ...) {
        char msg[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);
        Log("%s", msg);
    });
}

} // namespace eldenring::loader_detail
