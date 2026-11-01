#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

bool ChrCamPosAddress(uintptr_t& addr) {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t cam = 0;
    if (world == 0 || !SafeCopy(world + layout::kChrCamInWorldChrMan, &cam, sizeof(cam)) || cam == 0) return false;
    addr = static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30;
    return true;
}

bool CameraPositionTargetSafe(uintptr_t addr) {
    uint64_t probe = 0;
    float p0 = 0.f, p1 = 0.f;
    if (!SafeCopy(addr, &probe, sizeof(probe))) return false;
    std::memcpy(&p0, &probe, sizeof(p0));
    std::memcpy(&p1, reinterpret_cast<const uint8_t*>(&probe) + 4, sizeof(p1));
    if (probe >= g_img.base && probe < g_img.base + g_img.image_size) return false;
    if (!std::isfinite(p0) || !std::isfinite(p1)) return false;
    if (std::fabs(p0) >= 100000.f) return false;
    if (std::fabs(p1) <= 1e-6f || std::fabs(p1) >= 100000.f) return false;
    return true;
}

void RestoreCamKeep() {
    if (!g_cam_keep.valid) return;
    if (CameraPositionTargetSafe(g_cam_keep.pos_addr)) {
        WriteBytesSafe(g_cam_keep.pos_addr, g_cam_keep.third_person, sizeof(g_cam_keep.third_person));
    }
    g_cam_keep.valid = false;
}

void AfterCameraStep(bool have_before, uintptr_t addr, const float before[3]) {
    const unsigned call = ++g_camstep_calls;
    float now[3];
    if (!have_before || !SafeCopy(addr, now, sizeof(now))) return;
    const bool changed = std::fabs(now[0] - before[0]) + std::fabs(now[1] - before[1]) + std::fabs(now[2] - before[2]) > 1e-5f;
    if (changed) ++g_camstep_changed;
    float eye[3] = {};
    bool wrote = false;
    if (g_mode.modOwns(mc::ControlDomain::Camera) && g_fp_persist.load(std::memory_order_relaxed)) {
        const uintptr_t player = PlayerChrPtr();
        float feet[3];
        if (player != 0 && detail::readPhysicsPosition(g_reader, g_img.base, player, feet)) {
            firstPersonEye(feet, g_eye_height.load(), eye);
            if (!CameraPositionTargetSafe(addr)) {
                static std::atomic<unsigned> refused{0};
                const unsigned n = ++refused;
                if (n <= 8) {
                    Log("camstep: REFUSED eye write at %p (target no longer looks like a camera position)",
                        reinterpret_cast<void*>(addr));
                }
            } else if (WriteBytesSafe(addr, eye, sizeof(eye))) {
                g_cam_keep.pos_addr = addr;
                std::memcpy(g_cam_keep.third_person, now, sizeof(now));
                g_cam_keep.valid = true;
                wrote = true;
            }
        }
    }
    if (call <= 8 || call % 1200 == 0) {
        Log("camstep: call %u changed=%d (changed so far %u) engine pos=(%.2f %.2f %.2f)%s", call, changed ? 1 : 0, g_camstep_changed.load(), now[0], now[1], now[2],
            wrote ? " -> eye written" : "");
    }
}

} // namespace eldenring::loader_detail
