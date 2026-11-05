#include "core/loader_internal.hpp"
#include "cs_scope.hpp"
#include "input_hook.hpp"
#include "mc/rig/first_person_rig.hpp"

namespace eldenring::loader_detail {

namespace {
std::atomic<float> g_ow_shake_amp{0.f};
std::atomic<uint64_t> g_ow_shake_until_ms{0};
constexpr uint64_t kOwShakeMs = 350;

// AWP scope FOV. The render-camera copy at 0x4A7270 only copies the view
// matrix, not the FOV, so the zoom has to live in ChrCam+0x50 for the whole
// frame: write it after the camera update and restore the native value when
// the scope closes.
std::atomic<bool> g_scope_fov_wrote{false};
std::atomic<float> g_scope_fov_saved{0.f};
std::atomic<int> g_scope_fov_logged{-1};
} // namespace

void ApplyScopeFov(uintptr_t pos_addr) {
    const uintptr_t fov_addr = pos_addr + (layout::kCamFovY - layout::kCamMatrix - 0x30);
    const uint8_t level = g_mode.cs_scope_level.load(std::memory_order_relaxed);
    const bool scoped = g_mode.cs_mode.load(std::memory_order_relaxed) &&
                        g_mode.cs_weapon_slot.load(std::memory_order_relaxed) == 2 &&
                        eldenring::cs::scopeActive(level);
    if (scoped) {
        if (!g_scope_fov_wrote.load(std::memory_order_relaxed)) {
            float native = 0.f;
            if (SafeCopy(fov_addr, &native, sizeof(native))) {
                g_scope_fov_saved.store(native, std::memory_order_relaxed);
                g_scope_fov_wrote.store(true, std::memory_order_relaxed);
            }
        }
        const float deg = eldenring::cs::scopeFovDegrees(
            level, g_mode.cs_scope_fov1_deg.load(std::memory_order_relaxed),
            g_mode.cs_scope_fov2_deg.load(std::memory_order_relaxed));
        const float rad = deg * 3.14159265358979f / 180.f;
        SafeWrite(fov_addr, &rad, sizeof(rad));
        // CS2 scales scoped sensitivity by the FOV ratio; without this the
        // crosshair moves far too fast at zoom 2.
        const float native_rad = g_scope_fov_saved.load(std::memory_order_relaxed);
        const float native_deg = native_rad > 0.01f ? native_rad * 180.f / 3.14159265358979f : 48.f;
        const float sens = eldenring::cs::scopeSensitivityScale(
            level, g_mode.cs_scope_fov1_deg.load(std::memory_order_relaxed),
            g_mode.cs_scope_fov2_deg.load(std::memory_order_relaxed), native_deg,
            g_mode.cs_scope_sens_ratio.load(std::memory_order_relaxed));
        erin::SetMouseSensitivityScale(sens);
        const int key = static_cast<int>(level);
        if (g_scope_fov_logged.exchange(key, std::memory_order_relaxed) != key) {
            Log("scope fov: level %u -> %.2f deg (%.4f rad) at ChrCam+0x%X",
                static_cast<unsigned>(level), static_cast<double>(deg), static_cast<double>(rad),
                static_cast<unsigned>(layout::kCamFovY));
        }
    } else if (g_scope_fov_wrote.load(std::memory_order_relaxed)) {
        const float native = g_scope_fov_saved.load(std::memory_order_relaxed);
        SafeWrite(fov_addr, &native, sizeof(native));
        erin::SetMouseSensitivityScale(1.f);
        g_scope_fov_wrote.store(false, std::memory_order_relaxed);
        g_scope_fov_logged.store(-1, std::memory_order_relaxed);
        Log("scope fov: restored native %.4f rad (cs=%d slot=%u level=%u)", static_cast<double>(native),
            g_mode.cs_mode.load(std::memory_order_relaxed) ? 1 : 0,
            static_cast<unsigned>(g_mode.cs_weapon_slot.load(std::memory_order_relaxed)),
            static_cast<unsigned>(g_mode.cs_scope_level.load(std::memory_order_relaxed)));
    }
}

void TriggerOwShake(float amplitude) {
    if (!(amplitude > 0.f)) return;
    g_ow_shake_amp.store(amplitude, std::memory_order_relaxed);
    g_ow_shake_until_ms.store(GetTickCount64() + kOwShakeMs, std::memory_order_relaxed);
}

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
    ApplyScopeFov(addr);
    const bool changed = std::fabs(now[0] - before[0]) + std::fabs(now[1] - before[1]) + std::fabs(now[2] - before[2]) > 1e-5f;
    if (changed) ++g_camstep_changed;
    float eye[3] = {};
    bool wrote = false;
    if (g_mode.modOwns(mc::ControlDomain::Camera) && g_fp_persist.load(std::memory_order_relaxed)) {
        const uintptr_t player = PlayerChrPtr();
        float feet[3];
        if (player != 0 && detail::readPhysicsPosition(g_reader, g_img.base, player, feet)) {
            // The eye position is the shared rig policy; the guarded write and the deferred restore stay here.
            mc::rig::FirstPersonRig::eyeFromFeet(feet, g_eye_height.load(), eye);
            if (!CameraPositionTargetSafe(addr)) {
                static std::atomic<unsigned> refused{0};
                const unsigned n = ++refused;
                if (n <= 8) {
                    Log("camstep: REFUSED eye write at %p (target no longer looks like a camera position)",
                        reinterpret_cast<void*>(addr));
                }
            } else {
                const uint64_t now_ms = GetTickCount64();
                const uint64_t until = g_ow_shake_until_ms.load(std::memory_order_relaxed);
                if (now_ms < until) {
                    const float remain = static_cast<float>(until - now_ms) / static_cast<float>(kOwShakeMs);
                    const float amp = g_ow_shake_amp.load(std::memory_order_relaxed) * remain * remain;
                    const float phase = static_cast<float>(call) * 0.9f;
                    eye[0] += amp * std::sin(phase * 1.7f);
                    eye[1] += amp * std::sin(phase);
                }
                if (WriteBytesSafe(addr, eye, sizeof(eye))) {
                    g_cam_keep.pos_addr = addr;
                    std::memcpy(g_cam_keep.third_person, now, sizeof(now));
                    g_cam_keep.valid = true;
                    wrote = true;
                }
            }
        }
    }
    if (call <= 8 || call % 1200 == 0) {
        Log("camstep: call %u changed=%d (changed so far %u) engine pos=(%.2f %.2f %.2f)%s", call, changed ? 1 : 0, g_camstep_changed.load(), now[0], now[1], now[2],
            wrote ? " -> eye written" : "");
    }
}

} // namespace eldenring::loader_detail
