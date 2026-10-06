#include "{{GAME_NAME_LOWER}}_adapter.hpp"
#include "mc/animator.hpp"
#include "mc/voxel_world.hpp"
#include "mc/combat.hpp"
#include "mc/ballistics.hpp"

namespace {

std::unique_ptr<mc::adapter::{{GAME_NAME}}Adapter> g_adapter;
std::unique_ptr<mc::SteveAnimator> g_animator;
std::unique_ptr<mc::VoxelWorld> g_voxel_world;
std::unique_ptr<mc::CombatEngine> g_combat_engine;
std::unique_ptr<mc::BallisticsEngine> g_ballistics_engine;

bool g_steve_mode_active = false;

} // namespace

void OnModInitialize() {
    g_adapter = std::make_unique<mc::adapter::{{GAME_NAME}}Adapter>();
    g_animator = std::make_unique<mc::SteveAnimator>();
    g_voxel_world = std::make_unique<mc::VoxelWorld>(*g_adapter, *g_adapter);
    g_combat_engine = std::make_unique<mc::CombatEngine>(*g_adapter);
    g_ballistics_engine = std::make_unique<mc::BallisticsEngine>(*g_adapter);
}

void OnModShutdown() {
    if (g_steve_mode_active) {
        g_adapter->setNativePlayerVisible(true);
        g_adapter->destroySteveParts();
    }
    g_ballistics_engine.reset();
    g_combat_engine.reset();
    g_voxel_world.reset();
    g_animator.reset();
    g_adapter.reset();
}

void OnGameTick(float delta_time) {
    if (!g_steve_mode_active || !g_adapter) {
        return;
    }

    // 1. Gather player movement and input
    const mc::Vec3 vel = g_adapter->getPlayerVelocity();
    const mc::Vec3 cam_fwd = g_adapter->getCameraForward();

    mc::SteveAnimInput anim_input{};
    anim_input.forward_speed = vel.y; // Map according to engine axis convention
    anim_input.strafe_speed = vel.x;
    // Calculate pitch & yaw from cam_fwd
    anim_input.look_pitch = std::asin(cam_fwd.z);
    anim_input.look_yaw = std::atan2(cam_fwd.x, cam_fwd.y);

    // 2. Update math animator
    g_animator->update(delta_time, anim_input);

    // 3. Update 12 parts visual in game
    g_adapter->updateStevePartTransforms(g_animator->getTransforms());

    // 4. Update ballistics
    g_ballistics_engine->update(delta_time, g_adapter->getPlayerPosition());
}
