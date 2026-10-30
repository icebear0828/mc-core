#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

void DriveVelocityFromKeys(uintptr_t module, bool force_zero) {
    eldenring::movement::PlayerLocomotion::Instance().driveVelocity(module, force_zero, g_ctx);
}

uintptr_t FallModuleOf(uintptr_t chr) {
    return eldenring::movement::PlayerLocomotion::Instance().fallModuleOf(chr);
}

uintptr_t ActionFlagModuleOf(uintptr_t chr) {
    return eldenring::movement::PlayerLocomotion::Instance().actionFlagModuleOf(chr);
}

void UnlockAirMovement(uintptr_t chr) {
    eldenring::movement::PlayerLocomotion::Instance().unlockAirMovement(chr);
}

float FallTimer(uintptr_t chr) {
    return eldenring::movement::PlayerLocomotion::Instance().fallTimer(chr);
}

void WriteFallTimer(uintptr_t chr, float value) {
    eldenring::movement::PlayerLocomotion::Instance().writeFallTimer(chr, value);
    g_pre_fall_t.store(eldenring::movement::PlayerLocomotion::Instance().preFallTime(), std::memory_order_relaxed);
}

void ResetFallTimer(uintptr_t chr) {
    eldenring::movement::PlayerLocomotion::Instance().resetFallTimer(chr);
}

void LandFallTimer(uintptr_t chr) {
    eldenring::movement::PlayerLocomotion::Instance().landFallTimer(chr);
}

void ShadowGroundStep() {
    eldenring::world::VoxelWorldRuntime::Instance().stepShadowGround(g_ctx);
}

void BlocksCollisionStep() {
    eldenring::world::VoxelWorldRuntime::Instance().stepPlayerBlockCollision(g_ctx);
}

void GroundSprintStep(float dt) {
    eldenring::movement::PlayerLocomotion::Instance().stepGroundSprint(dt, g_ctx);
}

void McJumpStep() {
    static LARGE_INTEGER last_qpc{};
    LARGE_INTEGER now_qpc, freq;
    QueryPerformanceCounter(&now_qpc);
    QueryPerformanceFrequency(&freq);
    float dt = last_qpc.QuadPart != 0 ? static_cast<float>(now_qpc.QuadPart - last_qpc.QuadPart) / static_cast<float>(freq.QuadPart) : 0.f;
    last_qpc = now_qpc;
    dt = std::clamp(dt, 0.f, 0.05f);
    const uint64_t now_ms = GetTickCount64();
    g_movement_layer_ms.store(now_ms, std::memory_order_relaxed);
    eldenring::movement::PlayerLocomotion::Instance().stepJump(dt, now_ms, g_ctx);
}

void FlightStep() {
    static LARGE_INTEGER last_qpc{};
    LARGE_INTEGER now_qpc, freq;
    QueryPerformanceCounter(&now_qpc);
    QueryPerformanceFrequency(&freq);
    const float dt = last_qpc.QuadPart != 0 ? static_cast<float>(now_qpc.QuadPart - last_qpc.QuadPart) / static_cast<float>(freq.QuadPart) : 0.f;
    last_qpc = now_qpc;
    const uint64_t now_ms = GetTickCount64();
    g_movement_layer_ms.store(now_ms, std::memory_order_relaxed);
    auto& loco = eldenring::movement::PlayerLocomotion::Instance();
    loco.stepFlight(dt, now_ms, g_ctx);
    g_flying.store(loco.isFlying(), std::memory_order_relaxed);
    g_fly_yaw.store(loco.flyYaw(), std::memory_order_relaxed);
}

void InventoryTick(bool fg) {
    std::lock_guard<std::mutex> g(g_melee_mutex);
    eldenring::ui::InventoryController::Instance().tick(fg, WantSuppress(), PlayerDead(), g_melee.inventory(), g_ctx);
}

} // namespace eldenring::loader_detail
