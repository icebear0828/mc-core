#include "core/loader_internal.hpp"

#include <cstring>

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

// First-person sneak eye height: stepped every camera update, independent of block collision (which can early-return)
// and of how many blocks are placed, so the 1.65 m <-> 1.27 m transition also works on native terrain.
void SneakEyeHeightStep(float dt) {
    eldenring::world::VoxelWorldRuntime::Instance().stepSneakEyeHeight(dt, g_ctx);
}

void GroundSprintStep(float dt) {
    eldenring::movement::PlayerLocomotion::Instance().stepGroundSprint(dt, g_ctx);
}

void OwRocketPunchStep(float dt) {
#if ENABLE_OW_VIEWMODEL
    eldenring::movement::PlayerLocomotion::Instance().stepOwRocketPunch(dt, g_ctx);
#else
    (void)dt;
#endif
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

// Read-only diagnostic for the fall/ground state machine. Enabled by mc_er_fallprobe.txt and
// sampled from the existing camera-step and clamp hooks, so it never changes game state.
void FallProbeLog(const char* phase) {
    if (!g_fall_probe_log.load(std::memory_order_relaxed)) return;

    static uint64_t window_ms = 0;
    static uint32_t window_mask = 0;
    const uint64_t now = GetTickCount64();
    if (now - window_ms >= 100) {
        window_ms = now;
        window_mask = 0;
    }
    uint32_t bit = 1u << 3;
    if (std::strcmp(phase, "pre") == 0) bit = 1u << 0;
    else if (std::strcmp(phase, "post") == 0) bit = 1u << 1;
    else if (std::strcmp(phase, "clamp") == 0) bit = 1u << 2;
    if ((window_mask & bit) != 0) return;
    window_mask |= bit;

    const uintptr_t player = PlayerChrPtr();
    if (player == 0) return;
    const uintptr_t module = detail::readPhysicsModule(g_reader, g_img.base, player);
    if (module == 0) return;

    uint8_t b92 = 0, b93 = 0, b1d0 = 0, b1d1 = 0, b1d2 = 0, b1d5 = 0, b3f1 = 0, b3f2 = 0;
    uint32_t state = 0;
    float b1b8 = 0.f, b1cc = 0.f, b1c4 = 0.f;
    float pos[3] = {};
    SafeCopy(module + 0x92, &b92, 1);
    SafeCopy(module + 0x93, &b93, 1);
    SafeCopy(module + 0x1D0, &b1d0, 1);
    SafeCopy(module + 0x1D1, &b1d1, 1);
    SafeCopy(module + 0x1D2, &b1d2, 1);
    SafeCopy(module + 0x1D5, &b1d5, 1);
    SafeCopy(module + 0x3F1, &b3f1, 1);
    SafeCopy(module + 0x3F2, &b3f2, 1);
    SafeCopy(module + 0x388, &state, sizeof(state));
    SafeCopy(module + 0x1B8, &b1b8, sizeof(b1b8));
    SafeCopy(module + 0x1CC, &b1cc, sizeof(b1cc));
    SafeCopy(module + 0x1C4, &b1c4, sizeof(b1c4));
    detail::readPhysicsPosition(g_reader, g_img.base, player, pos);

    float fall_t = -1.f;
    if (const uintptr_t fall = FallModuleOf(player)) SafeCopy(fall + 0x18, &fall_t, sizeof(fall_t));
    uint64_t act = 0;
    if (const uintptr_t af = ActionFlagModuleOf(player)) SafeCopy(af + 0x40, &act, sizeof(act));

    Log("FALLPROBE phase=%s t=%llu pos=(%.2f %.2f %.2f) 92=%u 93=%u 1d0=%u 1d1=%u 1d2=%u 1d5=%u "
        "3f1=%u 3f2=%u 388=%u 1b8=%.3f 1cc=%.3f 1c4=%.3f fall=%.3f act=0x%llX",
        phase, static_cast<unsigned long long>(now),
        pos[0], pos[1], pos[2],
        static_cast<unsigned>(b92), static_cast<unsigned>(b93),
        static_cast<unsigned>(b1d0), static_cast<unsigned>(b1d1),
        static_cast<unsigned>(b1d2), static_cast<unsigned>(b1d5),
        static_cast<unsigned>(b3f1), static_cast<unsigned>(b3f2),
        state, b1b8, b1cc, b1c4, fall_t,
        static_cast<unsigned long long>(act));
}

} // namespace eldenring::loader_detail
