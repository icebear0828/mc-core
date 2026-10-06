#include "wukong_adapter.hpp"
#include "mc/animator.hpp"
#include "mc/voxel_world.hpp"
#include "mc/combat.hpp"
#include "mc/ballistics.hpp"

#include <memory>
#include <cmath>
#include <algorithm>

namespace {

std::unique_ptr<mc::adapter::WukongAdapter> g_adapter;
std::unique_ptr<mc::SteveAnimator> g_animator;
std::unique_ptr<mc::VoxelWorld> g_voxel_world;
std::unique_ptr<mc::CombatEngine> g_combat_engine;
std::unique_ptr<mc::BallisticsEngine> g_ballistics_engine;

bool g_steve_mode_active = false;

} // namespace

extern "C" {

void WukongMod_Initialize(b1::native::APlayerController* controller) {
    g_adapter = std::make_unique<mc::adapter::WukongAdapter>(controller);
    g_animator = std::make_unique<mc::SteveAnimator>();
    g_voxel_world = std::make_unique<mc::VoxelWorld>(*g_adapter, *g_adapter);
    g_combat_engine = std::make_unique<mc::CombatEngine>(*g_adapter);
    g_ballistics_engine = std::make_unique<mc::BallisticsEngine>(*g_adapter);
}

void WukongMod_Shutdown() {
    if (g_steve_mode_active && g_adapter) {
        g_adapter->setNativePlayerVisible(true);
        g_adapter->destroySteveParts();
    }
    g_ballistics_engine.reset();
    g_combat_engine.reset();
    g_voxel_world.reset();
    g_animator.reset();
    g_adapter.reset();
    g_steve_mode_active = false;
}

void WukongMod_SetSteveMode(bool active) {
    if (!g_adapter) {
        return;
    }
    g_steve_mode_active = active;
    if (active) {
        g_adapter->setNativePlayerVisible(false);
        g_adapter->spawnSteveParts();
    } else {
        g_adapter->setNativePlayerVisible(true);
        g_adapter->destroySteveParts();
    }
}

bool WukongMod_IsSteveModeActive() {
    return g_steve_mode_active;
}

void WukongMod_Tick(float delta_time) {
    if (!g_steve_mode_active || !g_adapter) {
        return;
    }

    // 1. Gather player movement and input
    const mc::Vec3 vel = g_adapter->getPlayerVelocity();
    const mc::Vec3 cam_fwd = g_adapter->getCameraForward();

    mc::SteveAnimInput anim_input{};
    // UE5 coordinate conventions: X forward, Y right, Z up
    anim_input.forward_speed = vel.x;
    anim_input.strafe_speed = vel.y;
    anim_input.look_pitch = std::asin(std::clamp(cam_fwd.z, -1.0f, 1.0f));
    anim_input.look_yaw = std::atan2(cam_fwd.y, cam_fwd.x);

    // 2. Update math animator
    g_animator->update(delta_time, anim_input);

    // 3. Update 12 parts visual in game
    g_adapter->updateStevePartTransforms(g_animator->getTransforms());

    // 4. Update ballistics
    g_ballistics_engine->update(delta_time, g_adapter->getPlayerPosition());
}

} // extern "C"

namespace mc::adapter {

WukongAdapter* GetGlobalWukongAdapter() {
    return g_adapter.get();
}

VoxelWorld* GetGlobalVoxelWorld() {
    return g_voxel_world.get();
}

CombatEngine* GetGlobalCombatEngine() {
    return g_combat_engine.get();
}

BallisticsEngine* GetGlobalBallisticsEngine() {
    return g_ballistics_engine.get();
}

} // namespace mc::adapter
