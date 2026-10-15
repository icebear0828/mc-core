#include "eldenring_hud_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

#if defined(WIN32) && !defined(MC_CORE_TESTS)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include "audio_xaudio2.hpp"
#include "overlay_d3d12.hpp"
#include "png_wic.hpp"
#include "mc/hud_atlas.hpp"
#include "eldenring_diagnostics.hpp"
#include "eldenring_hooks.hpp"
#include "eldenring_memory_scanner.hpp"
#include "eldenring_singletons.hpp"
#include "eldenring_state.hpp"
#include "eldenring_world.hpp"
#include "eldenring_model.hpp"
#include "eldenring_mobs.hpp"
#include "eldenring_locomotion.hpp"
#endif

namespace eldenring::ui {

void PopulateHudStateFromContext(erov::HudState& out, const RuntimeContext& ctx) {
    out.mc_mode = ctx.mode().mc_mode.load(std::memory_order_relaxed);
    out.creative = !creative::showsVitals(ctx.currentGameMode());
    const uint64_t toast_ms = ctx.mode().mode_toast_ms.load(std::memory_order_relaxed);
#if defined(WIN32)
    const uint64_t since = GetTickCount64() - toast_ms;
    out.mode_toast = (toast_ms != 0 && since < 2500) ? 1.f - static_cast<float>(since) / 2500.f : 0.f;
#else
    out.mode_toast = 0.f;
#endif
    out.slot_probe = ctx.ui().slot_probe.load(std::memory_order_relaxed);
    out.inv_open = ctx.ui().inv_open.load(std::memory_order_relaxed);
    out.mouse_x = ctx.ui().inv_mx.load(std::memory_order_relaxed);
    out.mouse_y = ctx.ui().inv_my.load(std::memory_order_relaxed);
    out.absorption_mc = ctx.player().survival.absorptionMc();
}

void PopulateSteveStateFromContext(erov::SteveState& steve, const RuntimeContext& ctx, bool dead, bool sleeping) {
    steve.dead = dead;
    steve.sleeping = sleeping;
    steve.shadow = ctx.mode().shadow_enabled.load(std::memory_order_relaxed);
    steve.ground_valid = ctx.player().ground_valid.load(std::memory_order_relaxed);
    steve.ground_y = ctx.player().ground_y.load(std::memory_order_relaxed);
    steve.first_person = ctx.mode().first_person.load(std::memory_order_relaxed) && ctx.ui().slot_probe.load(std::memory_order_relaxed) < 0;
    steve.speed_mps = ctx.mode().walk_speed.load(std::memory_order_relaxed);
    steve.on_ground = ctx.mode().on_ground.load(std::memory_order_relaxed);
    const bool sneaking = !dead && ctx.mode().sneak_active.load(std::memory_order_relaxed);
    const bool blocking = !dead && ctx.combat().shield.isBlocking();
    steve.sneaking = sneaking;
    steve.blocking = blocking;
    steve.sprinting = (!dead && !sneaking && !blocking) ? ctx.player().sprint_active.load(std::memory_order_relaxed) : false;
}

#if defined(WIN32) && !defined(MC_CORE_TESTS)

static void BridgeLog(const char* fmt, ...) {
    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    diagnostics::Log("%s", msg);
}

void RestoreNativeModel(std::unordered_map<uintptr_t, debug::HiddenWord>& hidden_flags, std::mutex& native_mutex) {
    std::lock_guard<std::mutex> lock(native_mutex);
    for (const auto& [a, w] : hidden_flags) {
        uint32_t flags = 0;
        if (memory::SafeCopy(a, &flags, sizeof(flags))) {
            memory::SafeWrite32(a, live::restoreBits(flags, w.original, w.mask));
        }
    }
    hidden_flags.clear();
}

void UpdateNativeModel(uintptr_t player, bool hide, const live::IMemoryReader& reader, uintptr_t base,
                       RuntimeContext& ctx, std::unordered_map<uintptr_t, debug::HiddenWord>& hidden_flags,
                       std::mutex& native_mutex) {
    std::lock_guard<std::mutex> lock(native_mutex);
    if (ctx.ui().slots_changed.exchange(false)) {
        for (const auto& [a, w] : hidden_flags) {
            uint32_t flags = 0;
            if (memory::SafeCopy(a, &flags, sizeof(flags))) {
                memory::SafeWrite32(a, live::restoreBits(flags, w.original, w.mask));
            }
        }
        hidden_flags.clear();
    }
    if (!hide) {
        for (const auto& [a, w] : hidden_flags) {
            uint32_t flags = 0;
            if (memory::SafeCopy(a, &flags, sizeof(flags))) {
                memory::SafeWrite32(a, live::restoreBits(flags, w.original, w.mask));
            }
        }
        hidden_flags.clear();
        return;
    }
    const std::vector<uintptr_t> addrs = live::collectDispFlagAddresses(reader, base, player, ctx.ui().hide_slots.load(std::memory_order_relaxed));
    const uint32_t masks[2] = {ctx.ui().hide_mask1.load(std::memory_order_relaxed), ctx.ui().hide_mask2.load(std::memory_order_relaxed)};
    for (uintptr_t b : addrs) {
        for (unsigned w = 0; w < 2; ++w) {
            const uintptr_t a = b + w * (live::layout::kDispFlags2 - live::layout::kDispFlags1);
            if (masks[w] == 0) continue;
            uint32_t flags = 0;
            if (!memory::SafeCopy(a, &flags, sizeof(flags))) continue;
            if ((flags & masks[w]) != 0) {
                hidden_flags.emplace(a, debug::HiddenWord{flags, masks[w]});
                memory::SafeWrite32(a, live::hideBits(flags, masks[w]));
            }
        }
    }
}

bool HudProvider(erov::HudState& out, erov::SteveState& steve) {
    auto& ctx = RuntimeContext::Instance();
    const memory::SelfReader reader;
    memory::ImageInfo img;
    if (!memory::ReadImageInfo(img)) return false;

    const uint32_t rva_world = ctx.memory().rva_world.load(std::memory_order_relaxed);
    if (rva_world == 0) return false;
    const uint32_t rva_menu = ctx.memory().rva_menu.load(std::memory_order_relaxed);
    const uint32_t rva_loading = ctx.memory().rva_loading.load(std::memory_order_relaxed);
    const uint32_t rva_fade = ctx.memory().rva_fade.load(std::memory_order_relaxed);

    const uintptr_t world = live::readSingleton(reader, img.base, rva_world, live::sigs::kWorldChrMan);
    if (world == 0) return false;
    uint64_t player = 0;
    live::Vitals v;
    if (!memory::SafeCopy(world + live::layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0 ||
        !live::readVitals(reader, img.base, static_cast<uintptr_t>(player), v)) {
        return false;
    }
    live::MenuState ms;
    live::LoadingState ls;
    float fade = 0.f;
    const bool have_state = (rva_menu != 0 && live::readMenuState(reader, live::readSingleton(reader, img.base, rva_menu, live::sigs::kCSMenuMan), ms)) &&
                            (rva_loading != 0 && live::readLoadingState(reader, live::readSingleton(reader, img.base, rva_loading, live::sigs::kCSNowLoadingHelper), ls)) &&
                            (rva_fade != 0 && live::readFadeAlpha(reader, live::readSingleton(reader, img.base, rva_fade, live::sigs::kCSFade), fade));

    PopulateHudStateFromContext(out, ctx);
    out.hp = v.hp;
    out.max_hp = v.max_hp;
    const bool steve_dead_now = v.max_hp > 0 && v.hp <= 0;
    {
        std::lock_guard<std::mutex> g(ctx.combat().melee_mutex);
        out.selected_slot = ctx.combat().melee.selectedSlot();
        for (int i = 0; i < mc::MeleeController::kSlots; ++i) {
            out.hotbar[i] = static_cast<uint16_t>(ctx.combat().melee.itemAt(i));
            out.hotbar_count[i] = static_cast<uint8_t>(std::min(255u, ctx.combat().melee.countAt(i)));
        }
        out.absorption_mc = ctx.player().survival.absorptionMc();
        out.eating = ctx.player().eating.getProgress();
        steve.held_item = static_cast<uint16_t>(ctx.combat().melee.heldItem());
        steve.offhand_item = static_cast<uint16_t>(ctx.combat().melee.inventory().offhand().item);
        steve.blocking = steve_dead_now ? false : ctx.mode().shield_blocking.load(std::memory_order_relaxed);
        steve.cooldown = ctx.combat().melee.cooldown();
        steve.eating = steve_dead_now ? 0.f : ctx.player().eating.getProgress();
        steve.bow_charge = steve_dead_now ? 0.f : ctx.mode().bow_power.load(std::memory_order_relaxed);
        steve.bow_offhand = ctx.mode().bow_offhand.load(std::memory_order_relaxed);
        steve.block_offhand = ctx.mode().shield_offhand.load(std::memory_order_relaxed);
        steve.bow_ticks = steve_dead_now ? 0.f : ctx.mode().bow_ticks.load(std::memory_order_relaxed);
        steve.sneaking = steve_dead_now ? false : ctx.mode().sneak_active.load(std::memory_order_relaxed);
        steve.sprinting = (steve_dead_now || steve.sneaking || steve.blocking) ? false : ctx.player().sprint_active.load(std::memory_order_relaxed);
        out.totem = ctx.combat().feedback.totem();
        steve.swing = steve_dead_now ? 0.f : ctx.combat().melee.swingProgress();
        out.hit = ctx.combat().feedback.hit();
        out.hit_crit = ctx.combat().feedback.crit();
        out.kill = ctx.combat().feedback.kill();
        const auto& at = ctx.combat().feedback.armorToast();
        out.armor_toast_timer = at.timer;
        out.armor_toast_raw = at.raw;
        out.armor_toast_final = at.final_dmg;
        out.armor_toast_pts = at.armor_pts;
        out.armor_toast_ratio = at.ratio;
        const mc::Inventory& inv = ctx.combat().melee.inventory();
        for (int i = 0; i < mc::Inventory::kSlots; ++i) {
            out.inv_item[i] = static_cast<uint16_t>(inv.slot(i).item);
            out.inv_count[i] = static_cast<uint8_t>(std::min(255u, inv.slot(i).count));
        }
        out.food = ctx.player().hunger.food();
        out.food_shaking = ctx.player().hunger.shaking();
        out.regen = ctx.player().survival.regenerating();
        out.xp_level = ctx.player().xp.level();
        out.xp_progress = ctx.player().xp.progress();
        out.cursor_item = static_cast<uint16_t>(inv.cursor().item);
        out.cursor_count = static_cast<uint8_t>(std::min(255u, inv.cursor().count));
        out.offhand_item = static_cast<uint16_t>(inv.offhand().item);
        out.offhand_count = static_cast<uint8_t>(std::min(255u, inv.offhand().count));
        for (int i = 0; i < mc::Inventory::kArmorSlots; ++i) {
            out.armor_item[i] = static_cast<uint16_t>(inv.armor(i).item);
            out.armor_count[i] = static_cast<uint8_t>(std::min(255u, inv.armor(i).count));
        }
        out.armor_points = inv.totalArmorPoints();
    }
    out.show = have_state && !ms.menu_focused && !ms.popup_open && !ls.screen_loading && fade < 0.02f;

    static std::unordered_map<uintptr_t, debug::HiddenWord> s_hidden_flags;
    static std::mutex s_native_mutex;
    if (ctx.mode().hide_native.load(std::memory_order_relaxed)) {
        const bool want_hidden = out.mc_mode && have_state && !ls.screen_loading;
        ctx.ui().native_hide_wanted.store(want_hidden);
        UpdateNativeModel(static_cast<uintptr_t>(player), want_hidden, reader, img.base, ctx, s_hidden_flags, s_native_mutex);
    }

    steve.draw = false;
    steve.cam_valid = false;
    live::CameraPose cam;
    float feet[3], q[4];
    if (ctx.mode().steve_enabled.load(std::memory_order_relaxed) && live::readCamera(reader, img.base, world, cam) &&
        live::detail::readPhysicsPosition(reader, img.base, static_cast<uintptr_t>(player), feet) &&
        live::detail::readPhysicsOrientation(reader, img.base, static_cast<uintptr_t>(player), q)) {
        steve.draw = true;
        steve.cam_valid = true;
        steve.cam.right = {cam.right[0], cam.right[1], cam.right[2]};
        steve.cam.up = {cam.up[0], cam.up[1], cam.up[2]};
        steve.cam.forward = {cam.forward[0], cam.forward[1], cam.forward[2]};
        if (ctx.mode().first_person.load(std::memory_order_relaxed)) {
            steve.cam.position = {feet[0], feet[1] + ctx.mode().eye_height.load(std::memory_order_relaxed), feet[2]};
        } else {
            steve.cam.position = {cam.position[0], cam.position[1], cam.position[2]};
        }
        steve.fov_y = cam.fov_y * ctx.player().sprint_fov.load(std::memory_order_relaxed);
        steve.feet[0] = feet[0];
        steve.feet[1] = feet[1];
        steve.feet[2] = feet[2];
        steve.yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]) + ctx.ui().steve_yaw_offset.load(std::memory_order_relaxed);
        if (ctx.mode().flying.load(std::memory_order_relaxed)) steve.yaw = ctx.ui().mob_yaw_offset.load(std::memory_order_relaxed);
        float grace_dist = 999.f;
        const bool near_grace = live::findNearestGrace(reader, img.base, world, feet, 2.8f, &grace_dist);
        steve.sleeping = near_grace && (ms.menu_focused || fade >= 0.02f);
    }
    {
        std::lock_guard<std::mutex> g(ctx.combat().melee_mutex);
        steve.hurt = ctx.combat().feedback.hurt();
    }
    steve.dead = steve_dead_now;
    steve.shadow = ctx.mode().shadow_enabled.load(std::memory_order_relaxed);
    steve.ground_valid = ctx.player().ground_valid.load(std::memory_order_relaxed);
    steve.ground_y = ctx.player().ground_y.load(std::memory_order_relaxed);
    steve.first_person = ctx.mode().first_person.load(std::memory_order_relaxed) && ctx.ui().slot_probe.load(std::memory_order_relaxed) < 0;
    steve.speed_mps = ctx.mode().walk_speed.load(std::memory_order_relaxed);
    steve.on_ground = ctx.mode().on_ground.load(std::memory_order_relaxed);
    if (steve.sleeping) {
        steve.first_person = false;
        steve.draw = true;
    } else if (ctx.ui().slot_probe.load(std::memory_order_relaxed) >= 0 || ctx.mode().first_person.load(std::memory_order_relaxed)) {
        steve.draw = false;
    }
    return true;
}

void SetupOverlay(const std::string& game_dir, RuntimeContext& ctx) {
    if (!hooks::FileExists(game_dir + "mc_er_overlay.txt")) {
        diagnostics::Log("overlay: disabled (no mc_er_overlay.txt)");
        return;
    }
    if (!hooks::EnsureMinHook()) {
        diagnostics::Log("overlay: MH_Initialize failed");
        return;
    }
    if (hooks::FileExists(game_dir + "mc_er_steve.txt")) {
        erov::SteveConfig cfg;
        std::vector<uint8_t> bytes;
        if (hooks::ReadFileAll(game_dir + "mc_er_steve.txt", bytes)) {
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
                else if (key == "yaw_offset_deg") ctx.ui().steve_yaw_offset.store(value * 3.14159265f / 180.f);
                else if (key == "mob_yaw_offset_deg") ctx.ui().mob_yaw_offset.store(value * 3.14159265f / 180.f);
                else if (key == "hide_native") ctx.mode().hide_native.store(value != 0.f);
                else if (key == "fp_persist") ctx.mode().fp_persist.store(value != 0.f);
                else if (key == "sound_volume") ctx.mode().sound_volume.store(value);
                else if (key == "no_player_hit_vfx") ctx.ui().no_player_hit_vfx.store(value != 0.f);
                else if (key == "first_person") ctx.mode().first_person.store(value != 0.f);
                else if (key == "shadow") ctx.mode().shadow_enabled.store(value != 0.f);
                else if (key == "eye_height") ctx.mode().eye_height.store(value);
                else if (key == "fall_reset") movement::PlayerLocomotion::Instance().setFallReset(value != 0.f);
                else if (key == "fall_hold") movement::PlayerLocomotion::Instance().setFallHold(std::clamp(value, 0.f, 2.8f));
                else if (key == "fly_speed") movement::PlayerLocomotion::Instance().setFlyHorizontalSpeed(std::clamp(value, 1.f, 40.f));
                else if (key == "fly_vspeed") movement::PlayerLocomotion::Instance().setFlyVerticalSpeed(std::clamp(value, 1.f, 40.f));
                else if (key == "fly_sync") movement::PlayerLocomotion::Instance().setFlySync(value != 0.f);
                else if (key == "mc_jump") movement::PlayerLocomotion::Instance().setMcJumpEnabled(value != 0.f);
                else if (key == "mc_jump_key") movement::PlayerLocomotion::Instance().setMcJumpVk(static_cast<int>(value));
                else if (key == "mc_jump_speed") movement::PlayerLocomotion::Instance().setMcJumpSpeed(std::clamp(value, 1.f, 20.f));
                else if (key == "mc_mode") ctx.mode().mc_mode.store(value != 0.f);
                else if (key == "blocks") ctx.mode().blocks_enabled.store(value != 0.f);
                else if (key == "block_drive") ctx.mode().block_drive.store(value != 0.f);
                else if (key == "instant_turn") ctx.mode().instant_turn.store(value != 0.f);
                else if (key == "turn_speed") ctx.mode().turn_speed.store(std::clamp(value, 0.5f, 50.f));
                else if (key == "block_speed" || key == "walk_speed") ctx.mode().block_walk_speed.store(std::clamp(value, 0.5f, 15.f));
                else if (key == "sneak_speed") ctx.mode().sneak_speed.store(std::clamp(value, 0.2f, 10.f));
                else if (key == "sprint_speed") ctx.mode().sprint_speed.store(std::clamp(value, 0.5f, 20.f));
                else if (key == "sprint_boost") movement::PlayerLocomotion::Instance().setSprintBoost(std::clamp(value, 0.0f, 20.f));
                else if (key == "sneak_key") ctx.mode().sneak_vk.store(static_cast<int>(value));
                else if (key == "disable_vanilla_roll" || key == "disable_roll") ctx.mode().disable_vanilla_roll.store(value != 0.f);
                else if (key == "roll_key") ctx.mode().roll_vk.store(static_cast<int>(value));
                else if (key == "block_anchor") ctx.mode().block_anchor.store(value != 0.f);
                else if (key == "block_ground") ctx.mode().block_ground.store(value != 0.f);
                else if (key == "block_collision") ctx.mode().block_collision.store(value != 0.f);
                else if (key == "enemy_block_collision" || key == "enemy_collision") ctx.mode().enemy_block_collision.store(value != 0.f);
                else if (key == "reach") ctx.mode().reach.store(std::clamp(value, 1.f, 8.f));
                else if (key == "inv_key") ctx.ui().inv_vk.store(static_cast<int>(value));
                else if (key == "inv_sens") ctx.ui().inv_sens.store(std::max(0.1f, value));
                else if (key == "kb_force") ctx.ui().kb_force.store(value);
                else if (key == "damage_scale") ctx.mode().damage_scale.store(std::clamp(value, 0.0f, 200.0f));
                else if (key == "damage_hp_percent") ctx.mode().damage_hp_percent.store(std::clamp(value, 0.0f, 1.0f));
                else if (key == "sweep_radius") ctx.mode().sweep_radius.store(std::clamp(value, 0.5f, 10.0f));
                else if (key == "hide_slots") ctx.ui().hide_slots.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
                else if (key == "hide_mask1") ctx.ui().hide_mask1.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
                else if (key == "hide_mask2") ctx.ui().hide_mask2.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
            }
        }
        cfg.depth_probe = hooks::FileExists(game_dir + "mc_er_depthprobe.txt");
        erov::SetSteveConfig(cfg);
        eraudio::Init((game_dir + "mods\\mc_adapter\\sounds\\").c_str(), ctx.mode().sound_volume.load(), BridgeLog);
        {
            std::vector<uint8_t> atlas;
            unsigned aw = 0, ah = 0;
            const std::string atlas_path_sub = game_dir + "mods\\mc_adapter\\mc_hud_atlas.png";
            const std::string atlas_path_root = game_dir + "mc_hud_atlas.png";
            const std::string atlas_path = hooks::FileExists(atlas_path_sub) ? atlas_path_sub : atlas_path_root;
            if (erov::DecodePngFile(atlas_path, atlas, aw, ah) && aw == mc::hud::kHudAtlasWidth && ah == mc::hud::kHudAtlasHeight) {
                erov::SetHudAtlas(atlas.data(), aw, ah);
                diagnostics::Log("hud atlas: external file %s (%ux%u)", atlas_path.c_str(), aw, ah);
            }
        }
        {
            std::vector<uint8_t> skin;
            unsigned sw = 0, sh = 0;
            const std::string skin_path = game_dir + "mods\\mc_adapter\\steve.png";
            if (erov::DecodePngFile(skin_path, skin, sw, sh) && sw == 64 && sh == 64) {
                erov::SetSteveSkin(skin.data(), sw, sh);
                diagnostics::Log("skin: external file %s (%ux%u)", skin_path.c_str(), sw, sh);
            }
        }
        {
            std::vector<uint8_t> json;
            std::string model_path = game_dir + "mods\\mc_adapter\\models\\creeper.geo.json";
            if (!hooks::FileExists(model_path)) {
                model_path = game_dir + "mods\\mc_adapter\\models\\zombie.geo.json";
            }
            std::optional<mc::model::EntityModel> model;
            if (hooks::ReadFileAll(model_path, json)) model = mc::model::EntityModel::fromJson(std::string_view(reinterpret_cast<const char*>(json.data()), json.size()));
            if (model && !model->bones.empty()) {
                erov::SetMobModel(*model);
                diagnostics::Log("mobs: model %s: %s, %zu bones", model_path.c_str(), model->identifier.c_str(), model->bones.size());
            }
        }
        {
            std::vector<uint8_t> mob_skin;
            unsigned zw = 0, zh = 0;
            std::string skin_path = game_dir + "mods\\mc_adapter\\creeper.png";
            if (!hooks::FileExists(skin_path)) {
                skin_path = game_dir + "mods\\mc_adapter\\zombie.png";
            }
            if (erov::DecodePngFile(skin_path, mob_skin, zw, zh) && zw == 64 && (zh == 64 || zh == 32)) {
                erov::SetMobSkin(mob_skin.data(), zw, zh);
                diagnostics::Log("skin: external file %s (%ux%u)", skin_path.c_str(), zw, zh);
            }
        }
        ctx.mode().steve_enabled.store(true);
    }
    erov::Install(&HudProvider, BridgeLog);
}

#endif

} // namespace eldenring::ui
