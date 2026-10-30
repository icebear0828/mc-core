#pragma once

// Elden Ring's mode -> control policy.
//
// Gather the current mode flags and host key ids (DirectInput scan codes) once per
// dispatcher tick, then build one mc::ControlPlan. Add a new mode here instead of
// adding another `if (mode)` branch to hotkey_dispatcher / player_locomotion.
//
// Pure logic: no Windows headers. The dispatcher converts virtual keys to scan
// codes and passes the spans in.

#include "mc/control_policy.hpp"

#include <span>

namespace eldenring::control {

// DirectInput scan codes for WASD. While the player stands on a placed block the mod's position drive is the
// only mover, so these are hidden from the native character controller (otherwise both move the player and
// the effective speed is about 2x).
inline constexpr int kBlockMoveKeys[] = {0x11 /*W*/, 0x1E /*A*/, 0x1F /*S*/, 0x20 /*D*/};

struct ModeInputs {
    bool foreground{true};
    bool input_enabled{true};
    bool inventory_open{false};
    bool native_input_allowed{false}; // WantSuppress(): no menu/loading screen
    bool mc_mode{false};
    bool cs_mode{false};
    bool ow_mode{false}; // Overwatch mode (Doomfist)
    bool first_person{false};

    bool mc_jump{false};
    int mc_jump_scan{0}; // 0 = do not mask the jump key

    bool block_movement{false}; // standing on a placed block: the position drive owns movement
    std::span<const int> block_move_keys{}; // DirectInput scan codes

    bool disable_vanilla_roll{false};
    std::span<const int> roll_keys{}; // DirectInput scan codes

    bool flying{false};
    std::span<const int> flight_keys{}; // DirectInput scan codes

    std::span<const int> cs_keys{}; // DirectInput scan codes owned by CS mode
    std::span<const int> ow_keys{}; // DirectInput scan codes owned by OW mode
};

inline mc::ControlPlan buildControlPlan(const ModeInputs& in) {
    mc::ControlResolver resolver;

    // Strict Mode Isolation:
    // When OW mode is active, it runs completely isolated from MC and CS modes.
    if (in.ow_mode && in.foreground) {
        if (in.input_enabled && in.native_input_allowed) {
            mc::requestMinecraftCombat(resolver);
        }
        if (!in.inventory_open) {
            for (const int key : in.ow_keys) {
                if (key > 0) resolver.request(mc::ControlDomain::Key, mc::ControlAuthority::Mod, 100, key, "ow mode");
            }
        }
        if (in.first_person) {
            mc::requestFirstPerson(resolver);
        }
        if (in.inventory_open) {
            mc::requestInventory(resolver);
        }
        return resolver.resolve();
    }

    // MC / CS combat owns the mouse while the game has no menu or loading screen.
    if ((in.mc_mode || in.cs_mode) && in.foreground && in.input_enabled && in.native_input_allowed) {
        mc::requestMinecraftCombat(resolver);
    }

    // CS mode owns the wheel and its action keys.
    if (in.cs_mode && in.foreground) {
        resolver.request(mc::ControlDomain::MouseWheel, mc::ControlAuthority::Mod, 100, 0, "cs mode");
        if (!in.inventory_open) {
            for (const int key : in.cs_keys) {
                if (key > 0) resolver.request(mc::ControlDomain::Key, mc::ControlAuthority::Mod, 100, key, "cs mode");
            }
        }
    }

    // First person owns the camera. Movement stays shared: the host still resolves
    // collision, the mod writes the desired velocity and body yaw.
    if (in.first_person) {
        mc::requestFirstPerson(resolver);
    }

    // Inventory UI owns mouse motion, the keyboard and the virtual cursor.
    if (in.inventory_open) {
        mc::requestInventory(resolver);
    }

    // MC jump / roll keys are hidden from the native game while MC mode is on.
    if (in.mc_mode && in.foreground && !in.inventory_open) {
        // The MC locomotion module writes velocity and body yaw, while the host
        // still resolves collision: both sides act.
        resolver.request(mc::ControlDomain::Movement, mc::ControlAuthority::Shared, 50, 0, "mc movement");
        if (in.mc_jump) {
            resolver.request(mc::ControlDomain::Jump, mc::ControlAuthority::None, 100, 0, "mc jump");
            if (in.mc_jump_scan > 0) {
                resolver.request(mc::ControlDomain::Key, mc::ControlAuthority::Mod, 100, in.mc_jump_scan, "mc jump");
            }
        }
        if (in.disable_vanilla_roll) {
            resolver.request(mc::ControlDomain::Roll, mc::ControlAuthority::None, 100, 0, "mc roll");
            for (const int key : in.roll_keys) {
                if (key > 0) resolver.request(mc::ControlDomain::Key, mc::ControlAuthority::Mod, 100, key, "mc roll");
            }
        }
    }

    // On a placed block the direct position drive is the only mover. Hide WASD from the native controller,
    // otherwise it adds its own movement and the player walks at roughly twice the configured speed.
    if (in.block_movement && in.mc_mode && in.foreground && !in.inventory_open && in.native_input_allowed) {
        resolver.request(mc::ControlDomain::Movement, mc::ControlAuthority::Mod, 80, 0, "block movement");
        for (const int key : in.block_move_keys) {
            if (key > 0) resolver.request(mc::ControlDomain::Key, mc::ControlAuthority::Mod, 80, key, "block movement");
        }
    }

    // Creative flight hides WASD / jump / crouch / sprint while it drives the player.
    if (in.flying && in.mc_mode) {
        resolver.request(mc::ControlDomain::Movement, mc::ControlAuthority::Mod, 100, 0, "creative flight");
        resolver.request(mc::ControlDomain::Jump, mc::ControlAuthority::Mod, 100, 0, "creative flight");
        for (const int key : in.flight_keys) {
            if (key > 0) resolver.request(mc::ControlDomain::Key, mc::ControlAuthority::Mod, 100, key, "creative flight");
        }
    }

    return resolver.resolve();
}

} // namespace eldenring::control
