#pragma once

// Callbacks shared by CombatService (melee/guard/damage) and ProjectileService (bullets/aim).
// Extracted verbatim from eldenring_combat_service.hpp so both services can copy it independently
// without one depending on the other's header.

#include "eldenring_camera.hpp"
#include "eldenring_live.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace eldenring::combat {

struct CombatCallbacks {
    std::function<uintptr_t()> get_player_chr;
    std::function<bool(uintptr_t chr, float feet[3])> read_physics_pos;
    std::function<bool(uintptr_t chr, float quat[4])> read_physics_orient;
    std::function<bool(eldenring::live::CameraPose& out_cam)> read_camera;
    std::function<bool(uintptr_t addr, void* dst, size_t size)> safe_read;
    std::function<bool(uintptr_t addr, const void* src, size_t size)> safe_write;
    std::function<bool(const float from[3], const float to[3])> has_line_of_sight;
    std::function<bool(uintptr_t chr, eldenring::live::Vitals& out_vitals)> read_vitals;
    std::function<void(uintptr_t chr, int new_hp)> set_chr_hp;
    std::function<bool(uintptr_t dmg_module, uintptr_t attacker, void* ctx_ptr)> call_vfunc7;
    std::function<void(const char* sound, float vol)> play_sound;
    std::function<void(int fx_kind, const float pos[3], int count)> spawn_fx;
    std::function<void(const float from[3], const float to[3], float life_s)> spawn_tracer;
#if ENABLE_CS2_VIEWMODEL
    // A bullet hole on static geometry: position and outward surface normal, both game metres.
    std::function<void(const float pos[3], const float normal[3])> spawn_decal;
#endif
    std::function<void(const std::string&)> log;
    const eldenring::live::IMemoryReader* reader{nullptr};
    uintptr_t image_base{0};
    std::function<uint32_t()> get_game_tid;
    std::function<bool(void* manager, uint32_t* out_handle, const void* req, void* status)> spawn_bullet;
    std::function<bool(uint8_t* ctx, uintptr_t atk, uintptr_t vic, int val, int* eng_val)> override_final_damage;
    std::function<bool()> is_falling;
    std::function<bool(const std::string& path)> file_exists;
};

} // namespace eldenring::combat
