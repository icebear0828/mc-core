// Elden Ring loader (milestone 1, step 1+2): a dinput8.dll proxy.
//   - always: logs the environment, binds the singletons by signature + RTTI, and logs a status line once a second
//     (read-only, nothing in the game is touched);
//   - if `mc_er_damage.txt` sits in the game folder: hooks CSChrDataModule::SetMaxHPAndClampHP and drains the damage
//     queue from it on the game thread; F8 queues a hit on the nearest hostile enemy (needs `mc_er_hit.bin`).
// Offline only: the loader refuses to do anything when an EAC module is loaded or steam_appid.txt is missing.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <unknwn.h>

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <share.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "mc/contracts/combat_adapter.hpp"

#include "input_hook.hpp"
#include "overlay_d3d12.hpp"
#include "png_wic.hpp"
#include "eldenring_camera.hpp"
#include "eldenring_damage.hpp"
#include "audio_xaudio2.hpp"
#include "eldenring_audio_data.hpp"
#include "eldenring_blockmesh.hpp"
#include "eldenring_blocks.hpp"
#include "eldenring_los.hpp"
#include "eldenring_survival.hpp"
#include "mc/consumables.hpp"
#include "mc/inventory_layout.hpp"
#include "mc/hud_atlas.hpp"
#include "mc/inventory.hpp"
#include "eldenring_melee.hpp"
#include "eldenring_particles.hpp"
#include "eldenring_model.hpp"
#include "eldenring_pick.hpp"
#include "eldenring_steve.hpp"
#include "eldenring_singletons.hpp"
#include "eldenring_state.hpp"
#include "eldenring_world.hpp"

using namespace eldenring::live;
namespace blocks = eldenring::blocks;

namespace {

// ---- logging -------------------------------------------------------------------------------------------------

std::mutex g_log_mutex;
FILE* g_log = nullptr;
std::string g_game_dir;
ULONGLONG g_start_ms = 0;

void Log(const char* fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    std::lock_guard<std::mutex> g(g_log_mutex);
    if (!g_log) return;
    fprintf(g_log, "[%8.3f] %s\n", static_cast<double>(GetTickCount64() - g_start_ms) / 1000.0, msg);
    fflush(g_log);
}

void InitLogOnce() {
    static std::mutex m;
    static bool done = false;
    std::lock_guard<std::mutex> g(m);
    if (done) return;
    done = true;
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    g_game_dir = exe;
    g_game_dir.resize(g_game_dir.find_last_of("\\/") + 1);
    g_start_ms = GetTickCount64();
    // _fsopen with _SH_DENYNO: other processes (the probes, `type`) can read the log while the game runs.
    g_log = _fsopen((g_game_dir + "mc_er.log").c_str(), "a", _SH_DENYNO);
    Log("==== eldenring adapter loaded (pid %lu, exe %s) ====", GetCurrentProcessId(), exe);
}

// ---- guarded memory access -----------------------------------------------------------------------------------

template <typename F>
bool SehGuard(F& f) {
    __try {
        f();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeCopy(uintptr_t address, void* out, size_t size) {
    if (size == 0) return true;
    if (address < 0x10000) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWrite32(uintptr_t address, uint32_t value) {
    if (address < 0x10000) return false;
    __try {
        *reinterpret_cast<volatile uint32_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

class SelfReader final : public IMemoryReader {
public:
    bool read(uintptr_t address, void* out, size_t size) const override { return SafeCopy(address, out, size); }
};

const SelfReader g_reader;

// ---- image ---------------------------------------------------------------------------------------------------

struct ImageInfo {
    uintptr_t base{0};
    uint32_t image_size{0};
    uint32_t text_rva{0};
    uint32_t text_size{0};
};

bool ReadImageInfo(ImageInfo& out) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base == 0) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<uintptr_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    out.base = base;
    out.image_size = nt->OptionalHeader.SizeOfImage;
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(sec[i].Name, ".text", 5) == 0) {
            out.text_rva = sec[i].VirtualAddress;
            out.text_size = sec[i].Misc.VirtualSize;
            return out.text_size > 0;
        }
    }
    return false;
}

struct Bound {
    uint32_t rva{0};
    bool ok{false};
};

// Locates one singleton global inside .text; any access fault counts as "not found yet" (the image may still be
// unpacking while the game starts).
Bound LocateSingleton(const ImageInfo& img, const SingletonSig& sig) {
    Bound b;
    std::optional<uint32_t> result;
    auto work = [&] {
        result = locateGlobalRva(reinterpret_cast<const uint8_t*>(img.base + img.text_rva), img.text_size, img.text_rva,
                                 img.image_size, sig);
    };
    if (SehGuard(work) && result) {
        b.rva = *result;
        b.ok = true;
    }
    return b;
}

bool LocateByPrefix(const ImageInfo& img, const char* pattern, uintptr_t& address) {
    const auto parsed = Signature::parse(pattern);
    if (!parsed) return false;
    ScanResult r;
    auto work = [&] { r = scanUnique(reinterpret_cast<const uint8_t*>(img.base + img.text_rva), img.text_size, *parsed); };
    if (!SehGuard(work) || r.status != ScanStatus::Unique) return false;
    address = img.base + img.text_rva + r.offset;
    return true;
}

// ---- game state ----------------------------------------------------------------------------------------------

ImageInfo g_img;
uint32_t g_rva_world = 0, g_rva_menu = 0, g_rva_loading = 0, g_rva_fade = 0;
std::atomic<DWORD> g_game_tid{0};

DWORD FindGameWindowThread() {
    struct Ctx {
        DWORD pid;
        DWORD tid;
    } ctx{GetCurrentProcessId(), 0};
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
            if (pid != c->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
            if (GetWindowTextLengthW(hwnd) <= 0) return TRUE;
            c->tid = tid;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.tid;
}

bool GameInForeground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// ---- damage --------------------------------------------------------------------------------------------------

std::atomic<bool> g_damage_enabled{false};
std::atomic<bool> g_mc_mode{false};
std::atomic<bool> g_input_enabled{false};
std::atomic<bool> g_click_attack{false};
std::atomic<bool> g_steve_enabled{false};
std::atomic<bool> g_hide_native{false};
struct HiddenWord {
    uint32_t original;
    uint32_t mask;
};
std::unordered_map<uintptr_t, HiddenWord> g_hidden_flags; // flag word addresses we cleared -> original value and the bits we cleared
std::mutex g_native_mutex; // g_hidden_flags: the Present thread and the pre-render hook both touch it
std::atomic<bool> g_native_hide_wanted{false}; // set by the HUD provider: the native model should be hidden this frame
std::atomic<unsigned> g_native_reshown{0};     // times the game turned a hidden part back on before we cleared it again
std::atomic<bool> g_slots_changed{false};
std::atomic<int> g_slot_probe{-1}; // F9 probe: -1 = off (all slots), 0..26 = only that part slot is hidden
std::atomic<uint32_t> g_hide_mask1{0x100A1}; // bits cleared in disp_flags1 (+0x20): visible + shadow (verified live, not bisected)
std::atomic<bool> g_no_player_hit_vfx{false}; // mc_er_steve.txt: no_player_hit_vfx=1 (also implied by first person): no blood when the player is hit
std::atomic<bool> g_fp_persist{true};      // mc_er_steve.txt: fp_persist=0 turns the persistent eye camera off
std::atomic<bool> g_first_person{false};  // mc_er_steve.txt: first_person=1 (experiment), F10 toggles
std::atomic<float> g_eye_height{1.65f};
std::atomic<float> g_kb_force{-1.f}; // mc_er_steve.txt: kb_force (>= 0 overrides HitContext+0xFC of our own hits; experiment)
std::atomic<uint32_t> g_hide_slots{0xFFFFFFFFu}; // bit n = part slot n of the native model (mc_er_steve.txt: hide_slots)
std::atomic<uint32_t> g_hide_mask2{1};         // bits cleared in disp_flags2 (+0x24)
float g_steve_yaw_offset = 3.14159265f; // the orientation quaternion at PhysicsModule+0x50 faces opposite to the model forward (user verified live, 180 deg)
std::atomic<bool> g_require_victim_updating{true};
DamageQueue g_queue;

// Attack rules come from mc::CombatEngine; hits are applied through g_queue, so this port only exists to build the engine.
class NoCombatPort : public mc::ICombatAdapter {
public:
    bool processHit(const mc::HitIntent&) override { return false; }
    float getMaxHealth(mc::EntityId) override { return 0.f; }
    void triggerStaggerOrRagdoll(mc::EntityId, const mc::Vec3&, float) override {}
};
NoCombatPort g_no_combat_port;
mc::CombatEngine g_combat_engine{g_no_combat_port};
std::mutex g_melee_mutex; // KeyThread writes, the Present thread reads
MeleeController g_melee;
JumpTracker g_jump; // KeyThread only
SurvivalState g_survival;          // regeneration / absorption, guarded by g_melee_mutex
mc::ConsumableSystem g_eating;     // right click on food, guarded by g_melee_mutex
std::atomic<float> g_sound_volume{0.8f}; // mc_er_steve.txt: sound_volume
std::atomic<float> g_walk_speed{0.f}; // horizontal speed of the player (m/s), measured by the key thread
std::atomic<bool> g_on_ground{true};
std::atomic<int> g_heal_pending{0}; // Elden Ring hit points waiting to be given back on the game thread
using ApplyHpFn = void*(__fastcall*)(void* data_module, int32_t hp, uint8_t flag);
ApplyHpFn g_apply_hp = nullptr;
HitFeedback g_feedback; // guarded by g_melee_mutex
thread_local int32_t t_hp_after = -1; // victim hp right after our vfunc[7] call (game thread)
std::optional<HitTemplate> g_template;
using ClampFn = void*(__fastcall*)(void*, int32_t);
ClampFn g_clamp_orig = nullptr;
using DamageFn = uint64_t(__fastcall*)(void*, void*, void*);

uint64_t NowTick() { return GetTickCount64() * 60ull / 1000ull; } // 1/60 s units

bool CallVfunc7(uintptr_t damage_module, uintptr_t attacker, void* ctx) {
    __try {
        void** vtable = *reinterpret_cast<void***>(damage_module);
        auto fn = reinterpret_cast<DamageFn>(vtable[layout::kDamageVfuncIndex]);
        fn(reinterpret_cast<void*>(damage_module), reinterpret_cast<void*>(attacker), ctx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---- first person experiment ----------------------------------------------------------------------------------------
// Detour of the function at 0x1404A7190 that copies ChrCam's vectors into another camera context. Before the copy the
// position in ChrCam is replaced by the player's eye position, after it the engine's own value is put back, so the
// camera controller and every other reader of ChrCam are untouched. Whether the renderer takes its view from that
// context is exactly what this experiment finds out.
using RenderCamCopyFn = void(__fastcall*)(void* self);
RenderCamCopyFn g_rcc_orig = nullptr;

void UpdateNativeModel(uintptr_t player, bool hide); // defined with the overlay code below

// (a function of its own: the one with __try cannot also hold an object with a destructor)
void ReadHurt(float& hurt, float& side) {
    std::lock_guard<std::mutex> g(g_melee_mutex);
    hurt = g_feedback.hurt();
    side = g_feedback.hurtSide();
}

void __fastcall RenderCamCopyDetour(void* self) {
    // Hide the native model right before the frame is drawn: the game may turn parts back on during a hit reaction, and the
    // Present-time write alone would let that frame show them.
    if (g_native_hide_wanted.load(std::memory_order_relaxed)) {
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        uint64_t player = 0;
        if (world != 0 && SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0) {
            UpdateNativeModel(static_cast<uintptr_t>(player), true);
        }
    }
    uintptr_t pos_addr = 0;
    uintptr_t axes_addr = 0;
    float saved[3] = {};
    float saved_axes[8] = {};
    if (g_first_person.load(std::memory_order_relaxed)) {
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        uint64_t cam = 0, player = 0;
        float feet[3];
        if (world != 0 && SafeCopy(world + layout::kChrCamInWorldChrMan, &cam, sizeof(cam)) && cam != 0 &&
            SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0 &&
            detail::readPhysicsPosition(g_reader, g_img.base, static_cast<uintptr_t>(player), feet) &&
            SafeCopy(static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30, saved, sizeof(saved))) {
            float eye[3];
            firstPersonEye(feet, g_eye_height.load(), eye);
            pos_addr = static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30;
            __try {
                std::memcpy(reinterpret_cast<void*>(pos_addr), eye, sizeof(eye));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                pos_addr = 0;
            }
            // Minecraft's hurt camera tilt: roll the view about the line of sight for a moment after a hit (right and up
            // turn about forward; both are put back after the copy).
            float hurt = 0.f, side = 1.f;
            ReadHurt(hurt, side);
            if (pos_addr != 0 && hurt > 0.f) {
                const uintptr_t axes = static_cast<uintptr_t>(cam) + layout::kCamMatrix; // right at +0x10, up at +0x20
                if (SafeCopy(axes, saved_axes, sizeof(saved_axes))) {
                    const float a = eldenring::fx::hurtTiltRadians(hurt, side), c = std::cos(a), s = std::sin(a);
                    float rolled[8];
                    for (int i = 0; i < 3; ++i) {
                        rolled[i] = saved_axes[i] * c + saved_axes[4 + i] * s;     // right
                        rolled[4 + i] = saved_axes[4 + i] * c - saved_axes[i] * s; // up
                    }
                    rolled[3] = saved_axes[3];
                    rolled[7] = saved_axes[7];
                    __try {
                        std::memcpy(reinterpret_cast<void*>(axes), rolled, sizeof(rolled));
                        axes_addr = axes;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                    }
                }
            }
        }
    }
    g_rcc_orig(self);
    if (pos_addr != 0) {
        __try {
            std::memcpy(reinterpret_cast<void*>(pos_addr), saved, sizeof(saved));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (axes_addr != 0) {
        __try {
            std::memcpy(reinterpret_cast<void*>(axes_addr), saved_axes, sizeof(saved_axes));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

// ---- no stagger (experiment, config no_stagger=1) -------------------------------------------------------------------------
// Every hit the player takes goes through one of two reaction pickers (REVERSE 21). While the experiment is on they do nothing for the
// player (the output byte is cleared), so the hit still costs hit points but plays no reaction animation. Game thread only.
uintptr_t PlayerChrPtr(); // defined below

using HitReactFn = void(__fastcall*)(void*, void*, void*, void*, uint64_t);
using HitReactHeavyFn = void(__fastcall*)(void*, void*, void*, void*, uint64_t, uint64_t);
HitReactFn g_hitreact_orig = nullptr;
HitReactHeavyFn g_hitreact_heavy_orig = nullptr;
std::atomic<bool> g_no_stagger{false};
std::atomic<unsigned> g_stagger_skipped{0};

bool IsPlayerActionModule(void* self) {
    const uintptr_t player = PlayerChrPtr();
    if (player == 0) return false;
    uint64_t container = 0, action = 0;
    if (!SafeCopy(player + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0 ||
        !SafeCopy(static_cast<uintptr_t>(container) + 0xA0, &action, sizeof(action))) {
        return false;
    }
    return static_cast<uintptr_t>(action) == reinterpret_cast<uintptr_t>(self);
}

void __fastcall HitReactDetour(void* self, void* a, void* b, void* out, uint64_t five) {
    if (g_no_stagger.load(std::memory_order_relaxed) && g_mc_mode.load(std::memory_order_relaxed) && IsPlayerActionModule(self)) {
        if (out != nullptr) *static_cast<uint8_t*>(out) = 0;
        if (++g_stagger_skipped <= 10) Log("no_stagger: skipped the default hit reaction picker for the player");
        return;
    }
    g_hitreact_orig(self, a, b, out, five);
}

void __fastcall HitReactHeavyDetour(void* self, void* a, void* b, void* out, uint64_t five, uint64_t six) {
    if (g_no_stagger.load(std::memory_order_relaxed) && g_mc_mode.load(std::memory_order_relaxed) && IsPlayerActionModule(self)) {
        if (out != nullptr) *static_cast<uint8_t*>(out) = 0;
        if (++g_stagger_skipped <= 10) Log("no_stagger: skipped the heavy hit reaction picker for the player");
        return;
    }
    g_hitreact_heavy_orig(self, a, b, out, five, six);
}

// ---- inventory screen ----------------------------------------------------------------------------------------------------
// While the inventory is open the game must not walk, roll, attack or turn the camera: the input master gate (0x14067B020) and the
// camera-rotation freeze test (0x140766C60) both report "blocked" (REVERSE 12). Both are optional: without them the mouse buttons and
// motion are still taken away from the game, and only the keyboard and gamepad keep moving the character.
std::atomic<bool> g_inv_open{false};
std::atomic<float> g_inv_mx{0.f}, g_inv_my{0.f}; // the virtual pointer, back buffer pixels
std::atomic<int> g_inv_vk{'I'};                  // mc_er_steve.txt: inv_key=<virtual key code>
std::atomic<float> g_inv_sens{1.f};              // pixels per mouse count
using IsInputBlockedFn = uint64_t(__fastcall*)();
using MenuFreezeFn = uint8_t(__fastcall*)(void*);
IsInputBlockedFn g_isblocked_orig = nullptr;
MenuFreezeFn g_menufreeze_orig = nullptr;
std::atomic<unsigned> g_isblocked_forced{0}, g_menufreeze_forced{0};

uint64_t __fastcall IsInputBlockedDetour() {
    if (g_inv_open.load(std::memory_order_relaxed)) {
        ++g_isblocked_forced;
        return 1;
    }
    return g_isblocked_orig();
}

uint8_t __fastcall MenuFreezeDetour(void* self) {
    if (g_inv_open.load(std::memory_order_relaxed)) {
        ++g_menufreeze_forced;
        return 1;
    }
    return g_menufreeze_orig(self);
}

// ---- persistent eye camera ----------------------------------------------------------------------------------------------
// ChrCam is read by more than the render copy: the effects and sound systems read its position through other functions, so a
// change that only lasts for the render copy leaves hit effects at the third-person position. Here the eye position is written
// right after the camera task has computed the frame's camera and stays there until the next camera task starts; the engine's
// own value is put back first so its camera logic never sees ours. Game thread only (the camera task runs there).
uintptr_t PlayerChrPtr(); // defined below

// (ChrCam* this, float dt in xmm1, ChrIns*, bool): the float is why the arguments must keep this exact shape.
using CameraStepFn = uint64_t(__fastcall*)(uint64_t, float, uint64_t, uint64_t);
CameraStepFn g_camstep_orig = nullptr;
struct CamKeep {
    uintptr_t pos_addr{0};
    float third_person[3]{};
    bool valid{false};
};
CamKeep g_cam_keep;
std::atomic<unsigned> g_camstep_calls{0}, g_camstep_changed{0};

bool WriteBytesSafe(uintptr_t address, const void* src, size_t n) {
    if (address < 0x10000) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ChrCamPosAddress(uintptr_t& addr) {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t cam = 0;
    if (world == 0 || !SafeCopy(world + layout::kChrCamInWorldChrMan, &cam, sizeof(cam)) || cam == 0) return false;
    addr = static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30;
    return true;
}

void RestoreCamKeep() {
    if (!g_cam_keep.valid) return;
    WriteBytesSafe(g_cam_keep.pos_addr, g_cam_keep.third_person, sizeof(g_cam_keep.third_person));
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
    if (g_first_person.load(std::memory_order_relaxed) && g_fp_persist.load(std::memory_order_relaxed)) {
        const uintptr_t player = PlayerChrPtr();
        float feet[3];
        if (player != 0 && detail::readPhysicsPosition(g_reader, g_img.base, player, feet)) {
            firstPersonEye(feet, g_eye_height.load(), eye);
            if (WriteBytesSafe(addr, eye, sizeof(eye))) {
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

uint64_t __fastcall CameraStepDetour(uint64_t self, float dt, uint64_t chr, uint64_t flag) {
    RestoreCamKeep();
    uintptr_t addr = 0;
    float before[3] = {};
    // Only the player's ChrCam is touched; the function runs for any camera object.
    const bool have = ChrCamPosAddress(addr) && addr == static_cast<uintptr_t>(self) + layout::kCamMatrix + 0x30 && SafeCopy(addr, before, sizeof(before));
    const uint64_t r = g_camstep_orig(self, dt, chr, flag);
    AfterCameraStep(have, addr, before);
    return r;
}


// ---- camera write watch (diagnostic, opt-in: mc_er_camwatch.txt) ----------------------------------------------------------
// Hardware write breakpoints on ChrCam's position find which instructions write it each frame. Every distinct writer is logged
// once with its module offset and the first stack words (return addresses), then counted.
struct CamWatchHit {
    uint64_t rip{0};
    uint64_t stack[6]{};
};
CamWatchHit g_watch_ring[256];
std::atomic<unsigned> g_watch_head{0};

LONG CALLBACK CamWatchVeh(PEXCEPTION_POINTERS info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || (info->ContextRecord->Dr6 & 0xF) == 0) return EXCEPTION_CONTINUE_SEARCH;
    const unsigned idx = g_watch_head.fetch_add(1) % 256;
    CamWatchHit& h = g_watch_ring[idx];
    h.rip = info->ContextRecord->Rip;
    const uint64_t* sp = reinterpret_cast<const uint64_t*>(info->ContextRecord->Rsp);
    for (int i = 0; i < 6; ++i) {
        uint64_t v = 0;
        SafeCopy(reinterpret_cast<uintptr_t>(sp + i), &v, sizeof(v));
        h.stack[i] = v;
    }
    info->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void ArmThreadWatch(DWORD tid, uintptr_t addr) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid);
    if (th == nullptr) return;
    if (SuspendThread(th) != static_cast<DWORD>(-1)) {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(th, &ctx)) {
            ctx.Dr0 = addr;
            ctx.Dr1 = addr + 8;
            ctx.Dr7 = 0x1ull | (1ull << 16) | (3ull << 18) | (1ull << 2) | (1ull << 20) | (3ull << 22);
            SetThreadContext(th, &ctx);
        }
        ResumeThread(th);
    }
    CloseHandle(th);
}

DWORD WINAPI CamWatchThread(LPVOID) {
    AddVectoredExceptionHandler(1, CamWatchVeh);
    std::unordered_map<DWORD, uintptr_t> armed;
    std::unordered_map<uint64_t, unsigned> counts;
    unsigned printed_head = 0, rounds = 0;
    for (;;) {
        Sleep(1000);
        ++rounds;
        uintptr_t addr = 0;
        if (ChrCamPosAddress(addr)) {
            if ((addr & 7) != 0) Log("camwatch: position address %p is not 8-aligned, watch not armed", reinterpret_cast<void*>(addr));
            else {
                const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                if (snap != INVALID_HANDLE_VALUE) {
                    THREADENTRY32 te{};
                    te.dwSize = sizeof(te);
                    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
                        auto it = armed.find(te.th32ThreadID);
                        if (it != armed.end() && it->second == addr) continue;
                        ArmThreadWatch(te.th32ThreadID, addr);
                        armed[te.th32ThreadID] = addr;
                    }
                    CloseHandle(snap);
                }
                if (rounds % 10 == 1) Log("camwatch: armed %zu threads on ChrCam pos %p", armed.size(), reinterpret_cast<void*>(addr));
            }
        }
        const unsigned head = g_watch_head.load();
        for (; printed_head < head && printed_head + 256 > head; ++printed_head) {
            const CamWatchHit& h = g_watch_ring[printed_head % 256];
            if (++counts[h.rip] == 1) {
                Log("camwatch: NEW writer rip=%p (RVA 0x%llX) stack: %llX %llX %llX %llX %llX %llX", reinterpret_cast<void*>(h.rip),
                    static_cast<unsigned long long>(h.rip - g_img.base), static_cast<unsigned long long>(h.stack[0] - g_img.base),
                    static_cast<unsigned long long>(h.stack[1] - g_img.base), static_cast<unsigned long long>(h.stack[2] - g_img.base),
                    static_cast<unsigned long long>(h.stack[3] - g_img.base), static_cast<unsigned long long>(h.stack[4] - g_img.base),
                    static_cast<unsigned long long>(h.stack[5] - g_img.base));
            }
        }
        if (rounds % 10 == 0) {
            for (const auto& kv : counts) Log("camwatch: writer RVA 0x%llX hit %u times", static_cast<unsigned long long>(kv.first - g_img.base), kv.second);
        }
    }
}

// ---- player hit effect (blood) -----------------------------------------------------------------------------------------
// 0x140450120 spawns the hit particles; its first gate is IsMainPlayer(victim), so it only runs for hits the player takes.
// For those it returns "nothing spawned" (0) without calling the game, when the experiment is on.
using HitVfxFn = uint8_t(__fastcall*)(void* damage_module, void* attacker, uint8_t* ctx, void* flags);
HitVfxFn g_hitvfx_orig = nullptr;
std::atomic<unsigned> g_hitvfx_skipped{0};

uint8_t __fastcall HitVfxDetour(void* a1, void* a2, uint8_t* ctx, void* flags) {
    // a1 is the ATTACKER's module (0x140448870 hands the attacker's slot-18 module down), so the victim is read from the
    // HitContext (+0x1E0), the same field our own hits fill in.
    uint64_t victim = 0, player = 0;
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    const bool known = world != 0 && SafeCopy(reinterpret_cast<uintptr_t>(ctx) + layout::kHitVictim, &victim, sizeof(victim)) &&
                       SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0;
    static std::atomic<unsigned> logged{0};
    if (known && logged.load() < 12 && (victim == player || logged.load() < 4)) {
        ++logged;
        Log("hit vfx: call a1=%p a2=%p ctx victim=%p player=%p -> %s", a1, a2, reinterpret_cast<void*>(victim), reinterpret_cast<void*>(player),
            victim == player ? "PLAYER IS THE VICTIM" : "other victim");
    }
    if (known && victim == player && (g_no_player_hit_vfx.load(std::memory_order_relaxed) || g_first_person.load(std::memory_order_relaxed))) {
        ++g_hitvfx_skipped;
        return 0;
    }
    return g_hitvfx_orig(a1, a2, ctx, flags);
}

// ---- line of sight (the game's static-geometry ray, docs/ELDENRING_REVERSE.md 1.6) -------------------------------------
struct alignas(16) HkVec4 {
    float v[4];
};
// bool wrapper(physWorld, filter, from*, disp*, outPos*, outNormal*, outFraction*): vectors must be 16-byte aligned
using RaycastFn = bool(__fastcall*)(void* phys_world, uint32_t filter, const HkVec4* from, const HkVec4* disp, HkVec4* out_pos,
                                    HkVec4* out_normal, float* out_fraction);
RaycastFn g_raycast = nullptr;
std::atomic<bool> g_los_enabled{false};
std::atomic<unsigned> g_los_blocked{0};

thread_local RayResult t_last_ray;

RayResult CastStaticRay(const float from[3], const float disp[3]) {
    t_last_ray = RayResult{};
    RayResult r;
    uint64_t havok = 0, world = 0;
    if (!SafeCopy(g_img.base + los::kHavokManGlobalRva, &havok, sizeof(havok)) || havok == 0) return r;
    uint64_t vt = 0;
    if (!SafeCopy(static_cast<uintptr_t>(havok), &vt, sizeof(vt)) || vt != g_img.base + los::kHavokManVtableRva) return r;
    if (!SafeCopy(static_cast<uintptr_t>(havok) + los::kPhysWorldInHavokMan, &world, sizeof(world)) || world == 0) return r;
    alignas(16) HkVec4 f = {{from[0], from[1], from[2], 0.f}};
    alignas(16) HkVec4 d = {{disp[0], disp[1], disp[2], 0.f}};
    alignas(16) HkVec4 pos = {}, normal = {};
    float fraction = 1.f;
    __try {
        r.hit = g_raycast(reinterpret_cast<void*>(world), los::kStaticGeometryFilter, &f, &d, &pos, &normal, &fraction);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_los_enabled.store(false); // never retry a ray that faulted
        return RayResult{};
    }
    r.ok = std::isfinite(fraction);
    r.fraction = fraction;
    for (int i = 0; i < 3; ++i) {
        r.pos[i] = pos.v[i];
        r.normal[i] = normal.v[i];
    }
    t_last_ray = r;
    return r;
}

bool PlayerCanSee(const float player_feet[3], const float victim_feet[3]) {
    const float from[3] = {player_feet[0], player_feet[1] + los::kEyeHeight, player_feet[2]};
    const float to[3] = {victim_feet[0], victim_feet[1] + los::kTargetHeight, victim_feet[2]};
    const bool visible = hasLineOfSight(CastStaticRay, from, to);
    const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    Log("los: dist=%.2f ray ok=%d hit=%d at=%.2f m -> %s", std::sqrt(dx * dx + dy * dy + dz * dz), t_last_ray.ok ? 1 : 0, t_last_ray.hit ? 1 : 0,
        t_last_ray.fraction * std::sqrt(dx * dx + dy * dy + dz * dz), visible ? "visible" : "BLOCKED");
    return visible;
}

const char* OutcomeName(DamageOutcome o) {
    switch (o) {
        case DamageOutcome::Applied: return "Applied";
        case DamageOutcome::Deferred: return "Deferred";
        case DamageOutcome::Expired: return "Expired";
        case DamageOutcome::NoTemplate: return "NoTemplate";
        case DamageOutcome::NoPlayer: return "NoPlayer";
        case DamageOutcome::VictimGone: return "VictimGone";
        case DamageOutcome::WrongModule: return "WrongModule";
        case DamageOutcome::VictimDead: return "VictimDead";
        case DamageOutcome::NotHostile: return "NotHostile";
        case DamageOutcome::BadDamage: return "BadDamage";
        case DamageOutcome::NoLineOfSight: return "NoLineOfSight";
    }
    return "?";
}

bool PlayerDead();

int32_t HpOf(uintptr_t chr) {
    Vitals v;
    return readVitals(g_reader, g_img.base, chr, v) ? v.hp : -1;
}

// Final-damage override. Armed by DrainOnce around our own vfunc[7] call on the game thread, so a natural hit that the
// game processes elsewhere (or even on this thread outside that call) is never touched.
struct ForcedDamage {
    bool armed{false};
    uint64_t attacker{0};
    uint64_t victim{0};
    int32_t value{0};
    bool applied{false};
    int32_t engine_value{0};
};
thread_local ForcedDamage t_forced;
using ProcessDamageFn = uint64_t (*)(void* module, void* attacker, uint8_t* ctx, uint32_t a4, uint8_t a5);
ProcessDamageFn g_pdc_orig = nullptr;
std::atomic<bool> g_pdc_hooked{false};
std::atomic<bool> g_log_player_hits{false}; // mc_er_hitlog.txt: log the HitContext of every hit the player takes (read only)

// Read-only: one line per hit the player takes, with the HitContext fields the reverser needs (blood effect, flinch, knockback).
uintptr_t DataModuleOfChr(uintptr_t chr);

void LogPlayerHit(void* module, void* attacker, const uint8_t* ctx, uint8_t blocked_flag) {
    uint64_t owner = 0, world = 0, player = 0;
    if (!SafeCopy(reinterpret_cast<uintptr_t>(module) + layout::kOwnerInDataModule, &owner, sizeof(owner)) || owner == 0) return;
    world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    if (world == 0 || !SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || owner != player) return;
    uint8_t b[0x270];
    if (!SafeCopy(reinterpret_cast<uintptr_t>(ctx), b, sizeof(b))) return;
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto f32 = [&](size_t o) { float v; std::memcpy(&v, b + o, 4); return v; };
    int32_t npc = 0;
    uint8_t team = 0;
    SafeCopy(reinterpret_cast<uintptr_t>(attacker) + layout::kNpcIdInChrIns, &npc, sizeof(npc));
    SafeCopy(reinterpret_cast<uintptr_t>(attacker) + layout::kTeamTypeInChrIns, &team, sizeof(team));
    Log("HIT-IN: dmg=%u poise=%u tier=%u kb+FC=%.2f f+50=%.2f %.2f %.2f u8[67]=%u u8[D9]=%u u8[DA]=%u u8[114]=%02X u8[115]=%02X u32[54]=%u "
        "u32[21C]=%u u32[230]=%u blocked_arg=%u attacker=%p npc=%d team=%u",
        u32(layout::kHitDamage), u32(0x40), u32(0x44), f32(layout::kHitKnockbackIn), f32(0x50), f32(0x54), f32(0x58), b[0x67], b[0xD9], b[0xDA],
        b[0x114], b[0x115], u32(0x54), u32(0x21C), u32(0x230), static_cast<unsigned>(blocked_flag), attacker, npc, static_cast<unsigned>(team));
    // What 0x447810 (which runs before this hook) left in the context: the stagger level and animation ids, and the data module
    // words around +0x154 that it compares with ctx+0x22C (REVERSE 21: poise or stamina?).
    uint32_t words[6] = {};
    if (const uintptr_t data = DataModuleOfChr(static_cast<uintptr_t>(owner))) SafeCopy(data + 0x148, words, sizeof(words));
    auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, b + o, 2); return static_cast<unsigned>(v); };
    Log("HIT-IN2: u32[22C]=%u u32[228]=%u anim[220]=%u [222]=%u [224]=%u [226]=%u u8[258]=%u u8[259]=%u u8[25A]=%u u8[266]=%02X u8[267]=%02X "
        "module+148..15C=%u %u %u %u %u %u",
        u32(0x22C), u32(0x228), u16(0x220), u16(0x222), u16(0x224), u16(0x226), b[0x258], b[0x259], b[0x25A], b[0x266], b[0x267], words[0], words[1],
        words[2], words[3], words[4], words[5]);
}

uintptr_t PlayerChrPtr() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t p = 0;
    return world != 0 && SafeCopy(world + layout::kPlayerInsInWorldChrMan, &p, sizeof(p)) ? static_cast<uintptr_t>(p) : 0;
}

bool PlayerDead() {
    const uintptr_t player = PlayerChrPtr();
    Vitals v;
    return player != 0 && readVitals(g_reader, g_img.base, player, v) && v.max_hp > 0 && v.hp <= 0;
}

uintptr_t DataModuleOfChr(uintptr_t chr) {
    uint64_t container = 0, data = 0;
    if (chr == 0 || !SafeCopy(chr + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0 ||
        !SafeCopy(static_cast<uintptr_t>(container) + layout::kChrDataModuleSlot * sizeof(uint64_t), &data, sizeof(data))) {
        return 0;
    }
    return static_cast<uintptr_t>(data);
}

// Gives hit points back through the game's own SetHP (bars and HUD follow). Game thread only.
bool GiveHp(uintptr_t data_module, int amount) {
    if (!g_apply_hp || data_module == 0 || amount <= 0) return false;
    int32_t hp = 0, max_hp = 0;
    if (!SafeCopy(data_module + layout::kDataHp, &hp, sizeof(hp)) || !SafeCopy(data_module + layout::kDataMaxHp, &max_hp, sizeof(max_hp)) || hp <= 0 ||
        max_hp <= 0) {
        return false;
    }
    __try {
        g_apply_hp(reinterpret_cast<void*>(data_module), std::min(max_hp, hp + amount), 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_apply_hp = nullptr; // never call it again after a fault
        return false;
    }
}

// Natural hits on the player: absorption soaks damage up, then a totem keeps a killing hit at 1 HP. Returns true when a totem was used.
bool PlayerHitSurvival(void* module, void* attacker, uint8_t* ctx, uint8_t blocked, uint32_t& popped_heal_target) {
    if (blocked != 0) return false; // a blocked hit deducts nothing
    uint64_t owner = 0;
    const uintptr_t player = PlayerChrPtr();
    if (player == 0 || !SafeCopy(reinterpret_cast<uintptr_t>(module) + layout::kOwnerInDataModule, &owner, sizeof(owner)) || owner != player) return false;
    Vitals v;
    if (!readVitals(g_reader, g_img.base, player, v) || v.hp <= 0 || v.max_hp <= 0) return false;
    int32_t dmg = 0;
    if (!SafeCopy(reinterpret_cast<uintptr_t>(ctx) + layout::kHitDamage, &dmg, sizeof(dmg)) || dmg <= 0) return false;
    bool popped = false;
    int32_t out = dmg;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        out = g_survival.absorb(dmg, v.max_hp);
        const TotemOutcome t = totemClamp(out, v.hp, g_melee.holds(mc::ItemId::TotemOfUndying));
        out = t.damage;
        if (t.popped && g_melee.consumeHeld(mc::ItemId::TotemOfUndying)) {
            popped = true;
            // Minecraft: Regeneration II for 45 s and Absorption II for 5 s, and the player is left on one heart (2 HP).
            g_survival.apply({mc::ActiveEffect{mc::EffectType::Regeneration, 2, 45.f}, mc::ActiveEffect{mc::EffectType::Absorption, 2, 5.f}});
            g_feedback.onTotem();
            popped_heal_target = static_cast<uint32_t>(std::max(1, scaleToEr(2.f, v.max_hp)));
        } else if (t.popped) {
            out = dmg; // the totem vanished meanwhile: the hit stands
        }
    }
    eraudio::Play(popped ? "item.totem.use" : "entity.player.hurt");
    {
        // Which way the hurt camera rolls: by the side the attacker stands on (relative to the way the player faces).
        float side = 1.f, pp[3], ap[3], q[4];
        if (attacker != nullptr && detail::readPhysicsPosition(g_reader, g_img.base, player, pp) &&
            detail::readPhysicsPosition(g_reader, g_img.base, reinterpret_cast<uintptr_t>(attacker), ap) &&
            detail::readPhysicsOrientation(g_reader, g_img.base, player, q)) {
            const float yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]);
            side = ((ap[0] - pp[0]) * std::cos(yaw) - (ap[2] - pp[2]) * std::sin(yaw)) >= 0.f ? 1.f : -1.f;
        }
        std::lock_guard<std::mutex> g(g_melee_mutex);
        g_feedback.onHurt(side);
    }
    if (out != dmg) SafeWrite32(reinterpret_cast<uintptr_t>(ctx) + layout::kHitDamage, static_cast<uint32_t>(out));
    if (out != dmg || popped) {
        Log("SURVIVAL: hit %d -> %d (hp %d/%d)%s", dmg, out, v.hp, v.max_hp, popped ? " TOTEM" : "");
    }
    return popped;
}

uint64_t ProcessDamageDetour(void* module, void* attacker, uint8_t* ctx, uint32_t a4, uint8_t a5) {
    uint32_t heal_to = 0;
    bool popped = false;
    if (!t_forced.armed) popped = PlayerHitSurvival(module, attacker, ctx, a5, heal_to);
    if (!t_forced.armed && g_log_player_hits.load(std::memory_order_relaxed)) LogPlayerHit(module, attacker, ctx, a5);
    if (t_forced.armed && !t_forced.applied) {
        t_forced.applied = overrideFinalDamage(ctx, t_forced.attacker, t_forced.victim, t_forced.value, &t_forced.engine_value);
    }
    const uint64_t r = g_pdc_orig(module, attacker, ctx, a4, a5);
    if (popped) {
        // Left on 1 HP by the clamp; the totem restores one heart. Still on the game thread, right after the engine finished the hit.
        const uintptr_t data = DataModuleOfChr(PlayerChrPtr());
        int32_t hp = 0;
        if (data != 0 && SafeCopy(data + layout::kDataHp, &hp, sizeof(hp)) && static_cast<int32_t>(heal_to) > hp) {
            GiveHp(data, static_cast<int>(heal_to) - hp);
        }
    }
    return r;
}

void DrainOnce(uintptr_t updating_data_module) {
    DrainContext c;
    c.reader = &g_reader;
    c.image_base = g_img.base;
    c.on_game_thread = true;
    c.tick = NowTick();
    c.updating_data_module = updating_data_module;
    c.require_victim_updating = g_require_victim_updating.load();
    c.hit_template = g_template ? &*g_template : nullptr;
    if (g_los_enabled.load()) c.line_of_sight = PlayerCanSee;
    c.max_per_drain = 1;
    c.expire_after_ticks = 60 * 5;
    c.invoke = [](uintptr_t dmg, uintptr_t attacker, void* ctx) {
        uint64_t owner = 0;
        SafeCopy(dmg + layout::kOwnerInDataModule, &owner, sizeof(owner));
        const int32_t before = HpOf(static_cast<uintptr_t>(owner));
        int32_t wanted = 0;
        std::memcpy(&wanted, static_cast<uint8_t*>(ctx) + layout::kHitDamage, sizeof(wanted));
        if (g_pdc_hooked.load()) {
            t_forced = ForcedDamage{};
            t_forced.armed = true;
            t_forced.attacker = attacker;
            t_forced.victim = owner;
            t_forced.value = wanted;
        }
        float kb_before = 0.f;
        const float kb_force = g_kb_force.load();
        if (kb_force >= 0.f) {
            kb_before = setKnockbackStrength(static_cast<uint8_t*>(ctx), kb_force);
            Log("KNOCKBACK: ctx+0xFC %.2f -> %.2f", kb_before, kb_force);
        }
        const bool ok = CallVfunc7(dmg, attacker, ctx);
        if (kb_force >= 0.f) {
            float kb_after = 0.f;
            int32_t out = 0;
            std::memcpy(&kb_after, static_cast<uint8_t*>(ctx) + layout::kHitKnockbackIn, sizeof(kb_after));
            std::memcpy(&out, static_cast<uint8_t*>(ctx) + layout::kHitKnockbackOut, sizeof(out));
            Log("KNOCKBACK: after the call ctx+0xFC=%.2f ctx+0x230=%d", kb_after, out);
        }
        const ForcedDamage forced = t_forced;
        t_forced = ForcedDamage{};
        const int32_t after = HpOf(static_cast<uintptr_t>(owner));
        t_hp_after = after;
        if (g_pdc_hooked.load()) {
            Log("DAMAGE: wanted=%d engine=%d override=%s hp delta=%d", wanted, forced.engine_value, forced.applied ? "applied" : "NOT applied",
                before - after);
        }
        uint32_t computed = 0;
        std::memcpy(&computed, static_cast<uint8_t*>(ctx) + layout::kHitDamage, sizeof(computed));
        if (!ok) {
            g_damage_enabled.store(false);
            Log("DAMAGE: vfunc[7] raised an exception on chr=%p, damage disabled", reinterpret_cast<void*>(owner));
        } else {
            Log("DAMAGE: vfunc[7] chr=%p hp %d -> %d (ctx+0x228 now %u) tid=%lu", reinterpret_cast<void*>(owner), before, after, computed,
                GetCurrentThreadId());
        }
    };
    t_hp_after = -1;
    for (const DamageResult& r : g_queue.drain(c)) {
        if (r.outcome == DamageOutcome::Applied) {
            std::lock_guard<std::mutex> g(g_melee_mutex);
            g_feedback.onHit((r.request.tag & 1u) != 0);
            // Minecraft's melee sounds: crit, sweep, otherwise strong (charged) or weak. A swing that hits nothing is silent.
            {
                // Minecraft's hit particles at the victim's chest: damage hearts (floor(damage * 0.5)), crit stars, the sweep arc.
                float vpos[3];
                if (detail::readPhysicsPosition(g_reader, g_img.base, r.request.victim_chr, vpos)) {
                    vpos[1] += 1.0f;
                    const int hearts = static_cast<int>((r.request.tag >> 8) & 0xFFu) / 2;
                    if (hearts > 0) erov::SpawnFx(erov::FxKind::Damage, vpos, hearts);
                    if (r.request.tag & 1u) erov::SpawnFx(erov::FxKind::Crit, vpos, 0);
                    if (r.request.tag & 2u) erov::SpawnFx(erov::FxKind::Sweep, vpos, 0);
                }
            }
            eraudio::Play((r.request.tag & 1u) ? "entity.player.attack.crit"
                          : (r.request.tag & 2u) ? "entity.player.attack.sweep"
                          : (r.request.tag & 4u) ? "entity.player.attack.strong"
                                                 : "entity.player.attack.weak");
            if (t_hp_after == 0) g_feedback.onKill(); // HP is clamped to [0, max]; -1 means unreadable, not a kill
        }
        if (r.outcome == DamageOutcome::NoLineOfSight) ++g_los_blocked;
        if (r.outcome != DamageOutcome::Applied) {
            Log("DAMAGE: request chr=%p -> %s", reinterpret_cast<void*>(r.request.victim_chr), OutcomeName(r.outcome));
        }
    }
}

void* __fastcall ClampDetour(void* module, int32_t value) {
    void* r = g_clamp_orig(module, value);
    if (g_damage_enabled.load(std::memory_order_relaxed) && GetCurrentThreadId() == g_game_tid.load(std::memory_order_relaxed) &&
        g_queue.pending() > 0) {
        DrainOnce(reinterpret_cast<uintptr_t>(module));
    }
    if (g_heal_pending.load(std::memory_order_relaxed) > 0 && GetCurrentThreadId() == g_game_tid.load(std::memory_order_relaxed)) {
        uint64_t owner = 0;
        if (SafeCopy(reinterpret_cast<uintptr_t>(module) + layout::kOwnerInDataModule, &owner, sizeof(owner)) && owner != 0 &&
            owner == PlayerChrPtr()) {
            const int h = g_heal_pending.exchange(0);
            if (h > 0) GiveHp(reinterpret_cast<uintptr_t>(module), h);
        }
    }
    return r;
}

void EnqueueNearestHostile() {
    std::vector<EnemyInfo> list;
    if (!enumerateEnemies(g_reader, g_img.base, list, 2000)) {
        Log("F8: enumeration failed (not in a world, or the position jumped)");
        return;
    }
    const EnemyInfo* best = nullptr;
    float best_d = 25.f;
    for (const EnemyInfo& e : list) {
        if (!e.hostile) continue;
        const float d = std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z);
        if (d <= best_d) {
            best_d = d;
            best = &e;
        }
    }
    if (best == nullptr) {
        Log("F8: no hostile enemy within 25 m");
        return;
    }
    const bool ok = g_queue.enqueue(best->chr, 50, NowTick());
    Log("F8: queued 50 on chr=%p npc=%d team=%u hp=%d dist=%.1f m (%s)", reinterpret_cast<void*>(best->chr), best->npc_id,
        static_cast<unsigned>(best->team), best->hp, best_d, ok ? "ok" : "queue full");
}

// MC left click: the hostile enemy under the crosshair, in melee reach.
void ClickAttack(float charged) {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    if (world == 0) return;
    CameraPose cam;
    uint64_t player = 0;
    float feet[3];
    if (!readCamera(g_reader, g_img.base, world, cam) || !SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) ||
        player == 0 || !detail::readPhysicsPosition(g_reader, g_img.base, static_cast<uintptr_t>(player), feet)) {
        Log("click: no camera or player");
        return;
    }
    std::vector<EnemyInfo> list;
    if (!enumerateEnemies(g_reader, g_img.base, list, 2000)) return;
    const float cam_rel[3] = {cam.position[0] - feet[0], cam.position[1] - feet[1], cam.position[2] - feet[2]};
    const int idx = pickTarget(cam_rel, cam.forward, list);
    if (idx < 0) {
        int near_count = 0;
        char detail_buf[400];
        int dn = snprintf(detail_buf, sizeof(detail_buf), "[");
        for (const EnemyInfo& e : list) {
            if (!e.hostile) continue;
            const float d = std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z);
            if (d > 8.f || near_count >= 3) continue;
            ++near_count;
            const float c[3] = {e.rel_x - cam_rel[0], e.rel_y + 1.f - cam_rel[1], e.rel_z - cam_rel[2]};
            const float t = c[0] * cam.forward[0] + c[1] * cam.forward[1] + c[2] * cam.forward[2];
            const float q[3] = {c[0] - cam.forward[0] * t, c[1] - cam.forward[1] * t, c[2] - cam.forward[2] * t};
            dn += snprintf(detail_buf + dn, sizeof(detail_buf) - static_cast<size_t>(dn), " (dist %.1f t %.1f off-ray %.1f)", d, t,
                           std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]));
        }
        snprintf(detail_buf + dn, sizeof(detail_buf) - static_cast<size_t>(dn), " ]");
        Log("click: no target (cam_rel %.1f %.1f %.1f, fwd %.2f %.2f %.2f, enemies<=8m: %s)", cam_rel[0], cam_rel[1], cam_rel[2],
            cam.forward[0], cam.forward[1], cam.forward[2], detail_buf);
        return;
    }
    const EnemyInfo& e = list[static_cast<size_t>(idx)];
    const bool falling = g_jump.jumping();
    mc::ItemId held;
    mc::HitIntent intent;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        held = g_melee.heldItem();
        intent = g_melee.makeIntent(g_combat_engine, mc::EntityId::LocalPlayer, mc::EntityId::None, charged, falling, !falling,
                                    {}, {cam.forward[0], cam.forward[1], cam.forward[2]});
    }
    const int dmg = erDamage(intent, e.max_hp);
    const bool ok = dmg > 0 && g_queue.enqueue(e.chr, dmg, NowTick(), (intent.is_critical ? 1u : 0u) | (intent.is_sweeping ? 2u : 0u) | (charged > 0.848f ? 4u : 0u) |
                                                          (static_cast<uint32_t>(std::min(255.f, std::floor(intent.damage))) << 8));
    Log("click: item=%d charged=%.2f falling=%d crit=%d sweep=%d mc_dmg=%.2f -> %d on chr=%p npc=%d hp=%d/%d (%s)", static_cast<int>(held),
        charged, falling ? 1 : 0, intent.is_critical ? 1 : 0, intent.is_sweeping ? 1 : 0, intent.damage, dmg,
        reinterpret_cast<void*>(e.chr), e.npc_id, e.hp, e.max_hp, ok ? "ok" : "not queued");
}

// Should the native character ignore the mouse buttons right now? Only while the MC HUD is on and the game shows no UI.
bool WantSuppress() {
    if (!g_mc_mode.load()) return false;
    MenuState ms;
    LoadingState ls;
    if (!readMenuState(g_reader, readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan), ms) ||
        !readLoadingState(g_reader, readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper), ls)) {
        return false;
    }
    return !ms.menu_focused && !ms.popup_open && !ls.screen_loading;
}

bool PlayerAirborne() {
    const uintptr_t w = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t p = 0;
    return w != 0 && SafeCopy(w + layout::kPlayerInsInWorldChrMan, &p, sizeof(p)) && p != 0 &&
           detail::readAirborne(g_reader, g_img.base, static_cast<uintptr_t>(p));
}

bool FileExists(const std::string& path); // defined below

// Diagnostic: +0x70 (position), +0x80 and +0x120 of the player's physics module, twice a second while moving. Answers whether
// +0x80 is the previous position (fromsoftware-rs: last_update_position) or a velocity (reverser 2026-10-09).
void LogPhysicsVectors() {
    static uint64_t last_ms = 0;
    const uint64_t now = GetTickCount64();
    if (now - last_ms < 500) return;
    last_ms = now;
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t player = 0;
    float p70[3], p80[3], p120[3];
    if (world == 0 || !SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0) return;
    const auto chr = static_cast<uintptr_t>(player);
    if (!detail::readPhysicsVec3(g_reader, g_img.base, chr, 0x70, p70) || !detail::readPhysicsVec3(g_reader, g_img.base, chr, 0x80, p80) ||
        !detail::readPhysicsVec3(g_reader, g_img.base, chr, 0x120, p120)) {
        return;
    }
    Log("phys: +70=(%.3f %.3f %.3f) +80=(%.3f %.3f %.3f) +120=(%.3f %.3f %.3f)", p70[0], p70[1], p70[2], p80[0], p80[1], p80[2], p120[0],
        p120[1], p120[2]);
}

// Diagnostic: the player's ground-contact bytes, one log line each time they change (jump / fall / roll / swim).
void LogGroundBytesOnChange() {
    static uint8_t last[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t player = 0;
    uint8_t b[4];
    if (world == 0 || !SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0 ||
        !detail::readGroundBytes(g_reader, g_img.base, static_cast<uintptr_t>(player), b)) {
        return;
    }
    if (std::memcmp(b, last, 4) == 0) return;
    std::memcpy(last, b, 4);
    Log("ground: 92=%u 93=%u 1D0=%u 1D1=%u", b[0], b[1], b[2], b[3]);
}

// F11: write down, right now, the state of every part slot of the player's native model, so a model that is visible in
// first person can be compared with what we believe is hidden.
void SnapshotNativeParts() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t player = 0, model = 0;
    if (world == 0 || !SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0 ||
        !SafeCopy(static_cast<uintptr_t>(player) + layout::kAsmModelInPlayerIns, &model, sizeof(model)) || model == 0) {
        Log("snapshot: no player model");
        return;
    }
    Log("snapshot: model=%p first_person=%d hide_wanted=%d slots=0x%X mask1=0x%X mask2=0x%X", reinterpret_cast<void*>(model),
        g_first_person.load() ? 1 : 0, g_native_hide_wanted.load() ? 1 : 0, g_hide_slots.load(), g_hide_mask1.load(), g_hide_mask2.load());
    std::lock_guard<std::mutex> lock(g_native_mutex);
    for (unsigned i = 0; i < layout::kAsmPartSlots; ++i) {
        uint64_t part = 0, disp = 0;
        uint32_t f1 = 0, f2 = 0;
        if (!SafeCopy(static_cast<uintptr_t>(model) + layout::kAsmPartPointers + i * 8, &part, sizeof(part)) || part == 0) continue;
        if (!SafeCopy(static_cast<uintptr_t>(part) + layout::kPartDispEntity, &disp, sizeof(disp)) || disp == 0) {
            Log("snapshot: slot %2u part=%p (no disp entity)", i, reinterpret_cast<void*>(part));
            continue;
        }
        SafeCopy(static_cast<uintptr_t>(disp) + layout::kDispFlags1, &f1, sizeof(f1));
        SafeCopy(static_cast<uintptr_t>(disp) + layout::kDispFlags2, &f2, sizeof(f2));
        const bool ours = g_hidden_flags.count(static_cast<uintptr_t>(disp) + layout::kDispFlags1) != 0;
        Log("snapshot: slot %2u part=%p disp=%p flags1=%08X flags2=%08X drawn=%d tracked=%d", i, reinterpret_cast<void*>(part),
            reinterpret_cast<void*>(disp), f1, f2, (f1 & layout::kDispVisibleBit) ? 1 : 0, ours ? 1 : 0);
    }
}

// Back to hiding every part slot: a probe left on a single slot would otherwise keep the rest of the model visible.
void ResetSlotProbe(int& cursor) {
    if (cursor < 0 && g_slot_probe.load() < 0 && g_hide_slots.load() == 0xFFFFFFFFu) return;
    cursor = -1;
    g_hide_slots.store(0xFFFFFFFFu);
    g_slot_probe.store(-1);
    g_slots_changed.store(true);
    Log("slots: probe reset, hiding ALL part slots");
}

// F9: hide one part slot at a time (to find out which body part a slot is), then all of them again.
void CycleHiddenSlot(int& cursor) {
    const uintptr_t w = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t p = 0;
    if (w == 0 || !SafeCopy(w + layout::kPlayerInsInWorldChrMan, &p, sizeof(p)) || p == 0) return;
    int next = cursor;
    for (int tries = 0; tries <= static_cast<int>(layout::kAsmPartSlots); ++tries) {
        next = next + 1;
        if (next >= static_cast<int>(layout::kAsmPartSlots)) {
            next = -1;
            break;
        }
        if (!collectDispFlagAddresses(g_reader, g_img.base, static_cast<uintptr_t>(p), 1u << next).empty()) break;
    }
    cursor = next;
    g_hide_slots.store(next < 0 ? 0xFFFFFFFFu : (1u << next));
    g_slot_probe.store(next);
    g_slots_changed.store(true);
    if (next < 0) {
        Log("slots: hiding ALL part slots");
    } else {
        Log("slots: hiding ONLY part slot %d (mask 0x%X)", next, 1u << next);
    }
}

// ---- placed blocks (phase A: our own grid, no physics bodies in the game) ----------------------------------------------------
// Right click with a block in hand places it, left click on a placed block breaks it. The game's physics knows nothing about the
// blocks: the player is kept out of them by writing the physics position (soft collision); enemies and arrows ignore them.
blocks::BlockGrid g_blocks;
std::mutex g_blocks_mutex;
std::atomic<bool> g_blocks_enabled{true};      // mc_er_steve.txt: blocks=0 turns placing and breaking off
std::atomic<bool> g_block_collision{true};     // block_collision=0: the player walks through the blocks
std::atomic<float> g_reach{4.5f};              // reach, metres (Minecraft survival)
unsigned g_blocks_sent_version = ~0u;

void SendBlockMesh() {
    std::lock_guard<std::mutex> g(g_blocks_mutex);
    if (g_blocks_sent_version == g_blocks.version()) return;
    g_blocks_sent_version = g_blocks.version();
    erov::SetBlockMesh(blocks::buildBlockMesh(g_blocks));
}

struct BlockTarget {
    bool have{false};
    bool on_block{false};        // the nearest surface is a placed block (else the game's terrain)
    blocks::Cell cell;           // the placed block, when on_block
    blocks::Cell place;          // where a new block would go
};

// The aim ray: from the player's eye along the camera's forward vector.
bool AimRay(float eye[3], float dir[3]) {
    const uintptr_t player = PlayerChrPtr();
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    CameraPose cam;
    float feet[3];
    if (player == 0 || world == 0 || !readCamera(g_reader, g_img.base, world, cam) || !detail::readPhysicsPosition(g_reader, g_img.base, player, feet)) return false;
    eye[0] = feet[0];
    eye[1] = feet[1] + g_eye_height.load();
    eye[2] = feet[2];
    for (int i = 0; i < 3; ++i) dir[i] = cam.forward[i];
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len > 0.5f)) return false;
    for (float& c : dir) c /= len;
    return true;
}

BlockTarget FindBlockTarget() {
    BlockTarget t;
    float eye[3], dir[3];
    if (!AimRay(eye, dir)) return t;
    const float reach = g_reach.load();
    std::optional<blocks::GridHit> grid_hit;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        grid_hit = blocks::raycastGrid(g_blocks, eye, dir, reach);
    }
    RayResult world_hit;
    if (g_los_enabled.load()) {
        const float disp[3] = {dir[0] * reach, dir[1] * reach, dir[2] * reach};
        world_hit = CastStaticRay(eye, disp);
    }
    const bool terrain = world_hit.ok && world_hit.hit;
    const float terrain_dist = terrain ? world_hit.fraction * reach : 1e9f;
    if (grid_hit && grid_hit->distance <= terrain_dist + 0.05f) {
        t.have = true;
        t.on_block = true;
        t.cell = grid_hit->cell;
        t.place = blocks::placementCellForBlockHit(*grid_hit);
    } else if (terrain) {
        t.have = true;
        t.place = blocks::placementCellForWorldHit(world_hit.pos, world_hit.normal);
    }
    return t;
}

const char* BlockSound(mc::BlockId id) {
    switch (id) {
        case mc::BlockId::Stone: return "block.stone.step";
        case mc::BlockId::Dirt: return "block.gravel.step";
        default: return "block.grass.step";
    }
}

// Right click with a block in hand. Returns true when the click was used (placed, or refused for a reason the player can see).
bool TryPlaceBlock() {
    if (!g_blocks_enabled.load()) return false;
    mc::ItemId held;
    int slot;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        held = g_melee.heldItem();
        slot = g_melee.selectedSlot();
        if (g_melee.countAt(slot) == 0) return false;
    }
    const mc::BlockId id = blocks::blockForItem(held);
    if (id == mc::BlockId::Air) return false;
    const BlockTarget t = FindBlockTarget();
    if (!t.have) return true;
    const uintptr_t player = PlayerChrPtr();
    float feet[3];
    if (player != 0 && detail::readPhysicsPosition(g_reader, g_img.base, player, feet) && blocks::cellTouchesPlayer(t.place, feet)) return true; // not inside yourself
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (!g_blocks.place(t.place, id)) return true;
    }
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        g_melee.consumeAt(slot);
        g_melee.startSwing();
    }
    SendBlockMesh();
    eraudio::Play(BlockSound(id), 0.8f);
    Log("blocks: placed %d at (%d %d %d)", static_cast<int>(id), t.place.x, t.place.y, t.place.z);
    return true;
}

// Left click on a placed block within reach: breaks it and puts the block in the inventory. Returns true when a block was broken.
bool TryBreakBlock() {
    if (!g_blocks_enabled.load()) return false;
    const BlockTarget t = FindBlockTarget();
    if (!t.have || !t.on_block) return false;
    mc::BlockId id;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        id = g_blocks.get(t.cell);
        if (!g_blocks.remove(t.cell)) return false;
    }
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        g_melee.inventory().add({blocks::itemForBlock(id), 1});
        g_melee.startSwing();
    }
    SendBlockMesh();
    eraudio::Play(BlockSound(id), 0.9f);
    Log("blocks: broke %d at (%d %d %d)", static_cast<int>(id), t.cell.x, t.cell.y, t.cell.z);
    return true;
}

// Called every key-thread tick: forgets the blocks when the game loads another map (the world coordinates are not the same any more),
// and keeps the player out of the blocks.
void BlocksTick() {
    static bool prev_loading = false;
    static unsigned pushes = 0;
    LoadingState ls;
    if (readLoadingState(g_reader, readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper), ls)) {
        if (ls.screen_loading && !prev_loading) {
            std::lock_guard<std::mutex> g(g_blocks_mutex);
            if (g_blocks.count() != 0) Log("blocks: loading screen, %zu blocks forgotten", g_blocks.count());
            g_blocks.clear();
        }
        prev_loading = ls.screen_loading;
    }
    SendBlockMesh();
    if (!g_block_collision.load() || !g_mc_mode.load()) return;
    const uintptr_t player = PlayerChrPtr();
    if (player == 0) return;
    float feet[3];
    if (!detail::readPhysicsPosition(g_reader, g_img.base, player, feet)) return;
    blocks::Resolve r;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (g_blocks.count() == 0) return;
        r = blocks::resolvePlayer(g_blocks, feet);
    }
    if (!r.moved) return;
    const uintptr_t module = detail::readPhysicsModule(g_reader, g_img.base, player);
    if (module == 0) return;
    // +0x70 is the position, +0x80 the previous frame's: both move, so the push is not seen as speed
    WriteBytesSafe(module + layout::kPhysicsPosition, r.feet, sizeof(r.feet));
    WriteBytesSafe(module + layout::kPhysicsPosition + 0x10, r.feet, sizeof(r.feet));
    if (++pushes <= 20 || pushes % 200 == 0) {
        Log("blocks: pushed the player out (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)%s, %u so far", feet[0], feet[1], feet[2], r.feet[0], r.feet[1], r.feet[2],
            r.standing ? " standing" : "", pushes);
    }
}

// Opens / closes the inventory and drives its virtual pointer. Runs on the key thread before the normal MC input, and takes the
// click edges while the screen is open so nothing else (attack, eating) reacts to them.
void InventoryTick(bool fg) {
    static bool prev_key = false, prev_esc = false;
    static bool prev_digit[mc::Inventory::kHotbar] = {};
    const bool want = fg && WantSuppress() && g_mc_mode.load() && !PlayerDead();
    bool open = g_inv_open.load();
    const bool key = fg && (GetAsyncKeyState(g_inv_vk.load()) & 0x8000) != 0;
    const bool esc = fg && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    auto closeIt = [&](const char* why) {
        {
            std::lock_guard<std::mutex> g(g_melee_mutex);
            g_melee.inventory().returnCursor();
        }
        g_inv_open.store(false);
        erin::SetSuppressMouseMotion(false);
        erin::SetSuppressKeyboard(false);
        open = false;
        Log("inventory: closed (%s)", why);
    };
    if (key && !prev_key) {
        if (open) {
            closeIt("key");
        } else if (want && g_input_enabled.load()) {
            float w = 0.f, h = 0.f;
            erov::ScreenSize(w, h);
            g_inv_mx.store(w * 0.5f);
            g_inv_my.store(h * 0.5f);
            int dx = 0, dy = 0;
            erin::TakeMouseDelta(dx, dy);
            erin::TakeLeftClick();
            erin::TakeRightClick();
            erin::TakeWheelNotches();
            erin::SetSuppressMouseMotion(true);
            erin::SetSuppressKeyboard(true);
            g_inv_open.store(true);
            open = true;
            Log("inventory: opened (%.0fx%.0f, gate calls so far %u)", w, h, g_isblocked_forced.load());
        }
    }
    if (open && esc && !prev_esc) closeIt("Esc");
    if (open && !want) closeIt("menu, loading or MC mode off");
    prev_key = key;
    prev_esc = esc;
    if (!open) {
        for (bool& d : prev_digit) d = false;
        return;
    }

    {
        static uint64_t last_ms = 0;
        const uint64_t now = GetTickCount64();
        if (now - last_ms >= 1000) {
            last_ms = now;
            unsigned keys = 0, motion = 0;
            erin::TakeSuppressStats(keys, motion);
            Log("inventory: open; blanked %u keyboard / %u mouse polls, input gate forced %u, camera freeze forced %u, player speed %.2f m/s", keys, motion,
                g_isblocked_forced.exchange(0), g_menufreeze_forced.exchange(0), g_walk_speed.load());
        }
    }
    float w = 0.f, h = 0.f;
    erov::ScreenSize(w, h);
    int dx = 0, dy = 0;
    erin::TakeMouseDelta(dx, dy);
    const float sens = g_inv_sens.load();
    const float mx = std::clamp(g_inv_mx.load() + static_cast<float>(dx) * sens, 0.f, std::max(0.f, w - 1.f));
    const float my = std::clamp(g_inv_my.load() + static_cast<float>(dy) * sens, 0.f, std::max(0.f, h - 1.f));
    g_inv_mx.store(mx);
    g_inv_my.store(my);
    erin::TakeWheelNotches();

    const mc::InventoryLayout lay(w, h);
    const mc::SlotRef ref = lay.hitTest(mx, my);
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    auto click = [&](mc::Button button) {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        mc::Inventory& inv = g_melee.inventory();
        switch (ref.kind) {
            case mc::SlotRef::Kind::Inventory: inv.click(ref.index, button, shift); break;
            case mc::SlotRef::Kind::Palette:
                if (static_cast<size_t>(ref.index) < mc::paletteItems().size()) inv.clickPalette(mc::paletteItems()[static_cast<size_t>(ref.index)], button, shift);
                break;
            case mc::SlotRef::Kind::None: inv.clickOutside(); break;
            case mc::SlotRef::Kind::Panel: break;
        }
    };
    if (erin::TakeLeftClick()) click(mc::Button::Left);
    if (erin::TakeRightClick()) click(mc::Button::Right);
    for (int i = 0; i < mc::Inventory::kHotbar; ++i) {
        const bool d = (GetAsyncKeyState('1' + i) & 0x8000) != 0;
        if (d && !prev_digit[i] && ref.kind == mc::SlotRef::Kind::Inventory) {
            std::lock_guard<std::mutex> g(g_melee_mutex);
            g_melee.inventory().swapWithHotbar(ref.index, i);
        }
        prev_digit[i] = d;
    }
}

DWORD WINAPI KeyThread(LPVOID) {
    bool prev8 = false, prev6 = false, prev7 = false;
    bool prev_digit[MeleeController::kSlots] = {};
    bool prev_space = false;
    bool prev9 = false;
    bool prev10 = false;
    bool prev11 = false;
    bool prev12 = false;
    int slot_cursor = -1; // -1 = all slots, 0..26 = only that part slot
    uint64_t last_ms = GetTickCount64();
    for (;;) {
        Sleep(15);
        const bool fg = GameInForeground();
        const uint64_t now_ms = GetTickCount64();
        const float dt = static_cast<float>(now_ms - last_ms) / 1000.f;
        last_ms = now_ms;
        {
            std::lock_guard<std::mutex> g(g_melee_mutex);
            g_melee.tick(dt);
            g_feedback.tick(dt);
        }
        if (g_mc_mode.load()) LogGroundBytesOnChange();
        if (g_mc_mode.load() && FileExists(g_game_dir + "mc_er_physlog.txt")) LogPhysicsVectors();
        if (g_mc_mode.load() && fg) {
            // Footsteps: Minecraft plays one every ~1.6 m walked on the ground. The ground material is not known yet: grass.
            static float prev_pos[3] = {};
            static bool have_prev = false;
            static eldenring::audio::StepClock steps;
            const uintptr_t player = PlayerChrPtr();
            float pos[3];
            if (player != 0 && detail::readPhysicsPosition(g_reader, g_img.base, player, pos)) {
                if (have_prev && dt > 0.f) {
                    const float dx = pos[0] - prev_pos[0], dz = pos[2] - prev_pos[2];
                    const float step_len = std::sqrt(dx * dx + dz * dz);
                    const bool grounded = !PlayerAirborne();
                    g_walk_speed.store(step_len < 1.5f ? step_len / dt : 0.f);
                    g_on_ground.store(grounded);
                    if (step_len < 1.5f && steps.update(step_len / dt, dt, grounded) && WantSuppress()) eraudio::Play("block.grass.step", 0.9f);
                    if (step_len >= 1.5f) steps = eldenring::audio::StepClock{}; // teleport / origin shift: not walking
                }
                std::memcpy(prev_pos, pos, sizeof(pos));
                have_prev = true;
            } else {
                have_prev = false;
            }
        }
        {
            // Regeneration, absorption and the meal being eaten. The healing itself is queued for the game thread.
            const uintptr_t player = PlayerChrPtr();
            Vitals v;
            if (player != 0 && readVitals(g_reader, g_img.base, player, v) && v.max_hp > 0) {
                int heal = 0;
                {
                    std::lock_guard<std::mutex> g(g_melee_mutex);
                    if (v.hp > 0) {
                        heal += g_survival.tick(dt, v.max_hp);
                        if (g_eating.isEating() && g_melee.heldItem() != g_eating.getCurrentItem()) g_eating.cancel(); // switched away
                        const mc::EatingEvent e = g_eating.update(dt);
                        if (e.chew_sound) eraudio::Play("entity.generic.eat", 0.8f);
                        if (e.completed) {
                            if (e.play_burp_sound) eraudio::Play("entity.player.burp");
                            g_survival.apply(e.effects);
                            heal += mealHeal(e, v.max_hp);
                            g_melee.consumeAt(g_melee.selectedSlot());
                            Log("SURVIVAL: finished eating (+%d now, regen/absorption from the item)", mealHeal(e, v.max_hp));
                        }
                    } else {
                        g_eating.cancel();
                    }
                }
                if (heal > 0) g_heal_pending.fetch_add(heal);
            }
        }
        InventoryTick(fg);
        BlocksTick();
        const bool inv_open = g_inv_open.load();
        const bool right_edge = !inv_open && g_input_enabled.load() && erin::TakeRightClick();
        if (right_edge && fg && WantSuppress() && !PlayerDead() && TryPlaceBlock()) {
            // a block was placed (or the click was refused): not a meal
        } else if (right_edge && fg && WantSuppress() && !PlayerDead()) {
            std::lock_guard<std::mutex> g(g_melee_mutex);
            const mc::ItemId item = g_melee.heldItem();
            if (!g_eating.isEating() && g_melee.countAt(g_melee.selectedSlot()) > 0 && g_eating.startEating(item)) {
                Log("SURVIVAL: started eating item %d", static_cast<int>(item));
            }
        }
        if (g_input_enabled.load()) {
            const int notches = erin::TakeWheelNotches();
            if (notches != 0 && fg && !inv_open && WantSuppress()) {
                std::lock_guard<std::mutex> g(g_melee_mutex);
                g_melee.scroll(-notches); // wheel away from the user = previous slot, as in Minecraft
            }
        }
        if (fg && !inv_open && WantSuppress()) {
            for (int i = 0; i < MeleeController::kSlots; ++i) {
                const bool d = (GetAsyncKeyState('1' + i) & 0x8000) != 0;
                if (d && !prev_digit[i]) {
                    std::lock_guard<std::mutex> g(g_melee_mutex);
                    g_melee.select(i);
                }
                prev_digit[i] = d;
            }
        }
        if (g_input_enabled.load()) erin::SetSuppressMouseButtons(fg && WantSuppress());
        const bool space = fg && (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        if (space && !prev_space && g_mc_mode.load()) Log("key: space down");
        g_jump.update(dt, space && !prev_space, g_mc_mode.load() && PlayerAirborne());
        prev_space = space;
        // A dead player cannot swing, eat or hit anything (the click edges are still taken so they do not pile up).
        const bool alive = !PlayerDead();
        const bool click_edge = !inv_open && g_input_enabled.load() && erin::TakeLeftClick() && alive;
        const bool want_suppress = click_edge ? WantSuppress() : false;
        if (click_edge && !(fg && g_click_attack.load() && want_suppress) && g_mc_mode.load()) {
            Log("click dropped: fg=%d click_attack=%d suppress=%d", fg ? 1 : 0, g_click_attack.load() ? 1 : 0, want_suppress ? 1 : 0);
        }
        if (click_edge && fg && g_click_attack.load() && want_suppress && TryBreakBlock()) {
            // a placed block was broken: the click is used up
        } else if (click_edge && fg && g_click_attack.load() && want_suppress) {
            float charged;
            {
                std::lock_guard<std::mutex> g(g_melee_mutex);
                charged = g_melee.startSwing();
            }
            Log("swing: charged=%.2f jumping=%d airborne=%d", charged, g_jump.jumping() ? 1 : 0, PlayerAirborne() ? 1 : 0);
            if (g_damage_enabled.load()) ClickAttack(charged);
        }
        const bool d8 = fg && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        const bool d6 = fg && (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        if (d8 && !prev8 && g_damage_enabled.load()) EnqueueNearestHostile();
        if (d6 && !prev6) {
            g_mc_mode.store(!g_mc_mode.load());
            Log("F6: MC mode %s", g_mc_mode.load() ? "on" : "off");
            ResetSlotProbe(slot_cursor);
        }
        const bool d9 = fg && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (d9 && !prev9 && g_hide_native.load()) CycleHiddenSlot(slot_cursor);
        prev9 = d9;
        const bool d12 = fg && (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        if (d12 && !prev12) {
            erov::RequestFrameTrace(240);
            Log("F12: tracing 240 frames of figure and camera positions");
        }
        prev12 = d12;
        const bool d11 = fg && (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        if (d11 && !prev11) SnapshotNativeParts();
        prev11 = d11;
        const bool d10 = fg && (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        if (d10 && !prev10) {
            g_first_person.store(!g_first_person.load());
            Log("F10: first person experiment %s", g_first_person.load() ? "on" : "off");
            ResetSlotProbe(slot_cursor);
        }
        prev10 = d10;
        const bool d7 = fg && (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        if (d7 && !prev7) erov::CycleDepthCandidate();
        prev7 = d7;
        prev8 = d8;
        prev6 = d6;
    }
}

bool EnsureMinHook() {
    static std::mutex m;
    static bool done = false, ok = false;
    std::lock_guard<std::mutex> g(m);
    if (!done) {
        const MH_STATUS st = MH_Initialize();
        ok = st == MH_OK || st == MH_ERROR_ALREADY_INITIALIZED;
        done = true;
    }
    return ok;
}

bool FileExists(const std::string& path) {
    const DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ReadFileAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    const size_t got = out.empty() ? 0 : fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

void SetupDamage() {
    if (!FileExists(g_game_dir + "mc_er_damage.txt")) {
        Log("damage: disabled (no mc_er_damage.txt)");
        return;
    }
    std::vector<uint8_t> bytes;
    if (!ReadFileAll(g_game_dir + "mc_er_hit.bin", bytes)) {
        Log("damage: mc_er_hit.bin missing, damage disabled");
        return;
    }
    g_template = HitTemplate::fromBytes(bytes.data(), bytes.size());
    if (!g_template) {
        Log("damage: mc_er_hit.bin has %zu bytes, expected %zu; damage disabled", bytes.size(), layout::kHitTemplateSize);
        return;
    }
    uintptr_t clamp = 0;
    if (!LocateByPrefix(g_img, sigs::kClampHp, clamp)) {
        Log("damage: ClampHP signature not unique, damage disabled");
        return;
    }
    if (!EnsureMinHook()) {
        Log("damage: MH_Initialize failed");
        return;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(clamp), reinterpret_cast<void*>(&ClampDetour), reinterpret_cast<void**>(&g_clamp_orig)) !=
            MH_OK ||
        MH_EnableHook(reinterpret_cast<void*>(clamp)) != MH_OK) {
        Log("damage: hooking ClampHP at %p failed", reinterpret_cast<void*>(clamp));
        return;
    }
    uintptr_t pdc = 0;
    if (!LocateByPrefix(g_img, sigs::kProcessDamageContext, pdc)) {
        Log("damage: ProcessDamageContext signature not unique, final damage stays engine-computed");
    } else if (MH_CreateHook(reinterpret_cast<void*>(pdc), reinterpret_cast<void*>(&ProcessDamageDetour), reinterpret_cast<void**>(&g_pdc_orig)) !=
                   MH_OK ||
               MH_EnableHook(reinterpret_cast<void*>(pdc)) != MH_OK) {
        Log("damage: hooking ProcessDamageContext at %p failed, final damage stays engine-computed", reinterpret_cast<void*>(pdc));
    } else {
        g_pdc_hooked.store(true);
        Log("damage: ProcessDamageContext hooked at %p (RVA 0x%llX): final damage is replaced for our own hits", reinterpret_cast<void*>(pdc),
            static_cast<unsigned long long>(pdc - g_img.base));
    }
    {
        uintptr_t rcc = 0;
        if (!LocateByPrefix(g_img, sigs::kRenderCameraCopy, rcc)) {
            Log("first person: render camera copy signature not unique, experiment unavailable");
        } else if (MH_CreateHook(reinterpret_cast<void*>(rcc), reinterpret_cast<void*>(&RenderCamCopyDetour), reinterpret_cast<void**>(&g_rcc_orig)) !=
                       MH_OK ||
                   MH_EnableHook(reinterpret_cast<void*>(rcc)) != MH_OK) {
            Log("first person: hooking %p failed", reinterpret_cast<void*>(rcc));
        } else {
            Log("first person: render camera copy hooked at %p (RVA 0x%llX); first_person=1 or F10 turns the experiment on", reinterpret_cast<void*>(rcc),
                static_cast<unsigned long long>(rcc - g_img.base));
        }
    }
    {
        uintptr_t hp_fn = 0;
        if (LocateByPrefix(g_img, sigs::kApplyHpChange, hp_fn)) {
            g_apply_hp = reinterpret_cast<ApplyHpFn>(hp_fn);
            Log("survival: ApplyHPChange at %p (RVA 0x%llX): food, regeneration and the totem heal through it", reinterpret_cast<void*>(hp_fn),
                static_cast<unsigned long long>(hp_fn - g_img.base));
        } else {
            Log("survival: ApplyHPChange signature not unique, no healing (the totem still clamps lethal damage)");
        }
    }
    g_log_player_hits.store(FileExists(g_game_dir + "mc_er_hitlog.txt"));
    if (g_log_player_hits.load()) Log("hitlog: logging the HitContext of every hit the player takes");
    {
        uintptr_t vfx = 0;
        if (!FileExists(g_game_dir + "mc_er_hitvfx.txt")) {
            // The game's own option (Settings: blood effects) already removes the blood, so this hook is opt-in.
        } else if (!LocateByPrefix(g_img, sigs::kHitVfxSpawn, vfx)) {
            Log("hit vfx: spawner signature not unique, the player's blood effect cannot be removed");
        } else if (MH_CreateHook(reinterpret_cast<void*>(vfx), reinterpret_cast<void*>(&HitVfxDetour), reinterpret_cast<void**>(&g_hitvfx_orig)) != MH_OK ||
                   MH_EnableHook(reinterpret_cast<void*>(vfx)) != MH_OK) {
            Log("hit vfx: hooking %p failed", reinterpret_cast<void*>(vfx));
        } else {
            Log("hit vfx: spawner hooked at %p (RVA 0x%llX); no_player_hit_vfx=1 or first person removes the player's hit effect",
                reinterpret_cast<void*>(vfx), static_cast<unsigned long long>(vfx - g_img.base));
        }
    }
    {
        uintptr_t step = 0;
        if (FileExists(g_game_dir + "mc_er_camwatch.txt")) {
            Log("camwatch: diagnostic on (mc_er_camwatch.txt): the camera update hook is not installed, writers of ChrCam position are logged");
            CreateThread(nullptr, 0, CamWatchThread, nullptr, 0, nullptr);
        } else if (!LocateByPrefix(g_img, sigs::kCameraStepExecute, step)) {
            Log("camstep: camera update signature not unique, hit effects stay at the third-person position in first person");
        } else if (MH_CreateHook(reinterpret_cast<void*>(step), reinterpret_cast<void*>(&CameraStepDetour), reinterpret_cast<void**>(&g_camstep_orig)) !=
                       MH_OK ||
                   MH_EnableHook(reinterpret_cast<void*>(step)) != MH_OK) {
            Log("camstep: hooking %p failed", reinterpret_cast<void*>(step));
        } else {
            Log("camstep: camera update hooked at %p (RVA 0x%llX): first person keeps the eye position for the whole frame (fp_persist=0 turns it off)",
                reinterpret_cast<void*>(step), static_cast<unsigned long long>(step - g_img.base));
        }
    }
    {
        uintptr_t gate = 0, freeze = 0;
        if (!LocateByPrefix(g_img, sigs::kIsInputBlocked, gate)) {
            Log("inventory: input gate signature not unique, the character keeps moving while the inventory is open");
        } else if (MH_CreateHook(reinterpret_cast<void*>(gate), reinterpret_cast<void*>(&IsInputBlockedDetour), reinterpret_cast<void**>(&g_isblocked_orig)) != MH_OK ||
                   MH_EnableHook(reinterpret_cast<void*>(gate)) != MH_OK) {
            Log("inventory: hooking the input gate %p failed", reinterpret_cast<void*>(gate));
        } else {
            Log("inventory: input gate hooked at %p (RVA 0x%llX)", reinterpret_cast<void*>(gate), static_cast<unsigned long long>(gate - g_img.base));
        }
        if (!LocateByPrefix(g_img, sigs::kMenuFreezesCamera, freeze)) {
            Log("inventory: camera freeze signature not unique, the camera keeps turning while the inventory is open");
        } else if (MH_CreateHook(reinterpret_cast<void*>(freeze), reinterpret_cast<void*>(&MenuFreezeDetour), reinterpret_cast<void**>(&g_menufreeze_orig)) != MH_OK ||
                   MH_EnableHook(reinterpret_cast<void*>(freeze)) != MH_OK) {
            Log("inventory: hooking the camera freeze %p failed", reinterpret_cast<void*>(freeze));
        } else {
            Log("inventory: camera freeze hooked at %p (RVA 0x%llX)", reinterpret_cast<void*>(freeze), static_cast<unsigned long long>(freeze - g_img.base));
        }
    }
    {
        uintptr_t react = 0, heavy = 0;
        if (!LocateByPrefix(g_img, sigs::kHitReactDefault, react) || !LocateByPrefix(g_img, sigs::kHitReactHeavy, heavy)) {
            Log("no_stagger: hit reaction signatures not unique, the experiment is unavailable");
        } else if (MH_CreateHook(reinterpret_cast<void*>(react), reinterpret_cast<void*>(&HitReactDetour), reinterpret_cast<void**>(&g_hitreact_orig)) != MH_OK ||
                   MH_EnableHook(reinterpret_cast<void*>(react)) != MH_OK ||
                   MH_CreateHook(reinterpret_cast<void*>(heavy), reinterpret_cast<void*>(&HitReactHeavyDetour), reinterpret_cast<void**>(&g_hitreact_heavy_orig)) != MH_OK ||
                   MH_EnableHook(reinterpret_cast<void*>(heavy)) != MH_OK) {
            Log("no_stagger: hooking the hit reaction pickers failed");
        } else {
            Log("no_stagger: reaction pickers hooked at %p / %p (RVA 0x%llX / 0x%llX); no_stagger=1 in mc_er_steve.txt turns the experiment on",
                reinterpret_cast<void*>(react), reinterpret_cast<void*>(heavy), static_cast<unsigned long long>(react - g_img.base),
                static_cast<unsigned long long>(heavy - g_img.base));
        }
    }
    if (FileExists(g_game_dir + "mc_er_nolos.txt")) {
        Log("los: disabled by mc_er_nolos.txt");
    } else {
        uintptr_t ray = 0;
        if (!LocateByPrefix(g_img, sigs::kRaycastWrapper, ray)) {
            Log("los: raycast wrapper signature not unique, walls do not block hits");
        } else {
            g_raycast = reinterpret_cast<RaycastFn>(ray);
            g_los_enabled.store(true);
            Log("los: raycast wrapper at %p (RVA 0x%llX), filter 0x%X, walls block hits", reinterpret_cast<void*>(ray),
                static_cast<unsigned long long>(ray - g_img.base), los::kStaticGeometryFilter);
        }
    }
    // mc_er_anyvictim.txt: do not wait for the victim to be the entity being updated (lower latency, small race risk).
    g_require_victim_updating.store(!FileExists(g_game_dir + "mc_er_anyvictim.txt"));
    g_damage_enabled.store(true);
    Log("damage: enabled; ClampHP hooked at %p (RVA 0x%llX); press F8 to hit the nearest hostile enemy (require_victim_updating=%d)", reinterpret_cast<void*>(clamp),
        static_cast<unsigned long long>(clamp - g_img.base), g_require_victim_updating.load() ? 1 : 0);
}

// ---- overlay -------------------------------------------------------------------------------------------------

// Runs on the Present thread. The HUD is hidden whenever the game shows its own full-screen UI.
// Clears (hide) or restores (show) the "drawn" bit of every part of the player's native model. Called every frame from
// the Present thread while the feature is on; the part list is re-read each time, so parts the game replaced are
// never written through a stale address.
// Puts back every bit we cleared, wherever it was (not only in the slots selected right now).
void RestoreNativeModel() {
    for (const auto& [a, w] : g_hidden_flags) {
        uint32_t flags = 0;
        if (SafeCopy(a, &flags, sizeof(flags))) SafeWrite32(a, restoreBits(flags, w.original, w.mask));
    }
    g_hidden_flags.clear();
}

void UpdateNativeModel(uintptr_t player, bool hide) {
    std::lock_guard<std::mutex> lock(g_native_mutex);
    if (g_slots_changed.exchange(false)) RestoreNativeModel(); // another slot set was chosen: show the old one again
    if (!hide) {
        RestoreNativeModel();
        return;
    }
    const std::vector<uintptr_t> addrs = collectDispFlagAddresses(g_reader, g_img.base, player, g_hide_slots.load());
    {
        static std::vector<uintptr_t> last_set;
        if (addrs != last_set) {
            size_t fresh = 0;
            for (uintptr_t a : addrs) fresh += std::find(last_set.begin(), last_set.end(), a) == last_set.end() ? 1 : 0;
            if (!last_set.empty()) Log("native: part set changed: %zu parts, %zu new address(es) (was %zu)", addrs.size(), fresh, last_set.size());
            last_set = addrs;
        }
    }
    const uint32_t masks[2] = {g_hide_mask1.load(), g_hide_mask2.load()};
    for (uintptr_t base : addrs) {
        for (unsigned w = 0; w < 2; ++w) {
            const uintptr_t a = base + w * (layout::kDispFlags2 - layout::kDispFlags1);
            if (masks[w] == 0) continue;
            uint32_t flags = 0;
            if (!SafeCopy(a, &flags, sizeof(flags))) continue;
            if ((flags & masks[w]) != 0) {
                if (!g_hidden_flags.emplace(a, HiddenWord{flags, masks[w]}).second) ++g_native_reshown; // we had hidden it, the game showed it again
                SafeWrite32(a, hideBits(flags, masks[w]));
            }
        }
    }
}

bool HudProvider(erov::HudState& out, erov::SteveState& steve) {
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
    out.mc_mode = g_mc_mode.load();
    out.slot_probe = g_slot_probe.load();
    {
        // CSMenuMan+0x654C was a candidate for the game's HUD option, but it reads 0x3D240000 (a float-looking value) and never
        // changes, so it is NOT a 0/1/2 setting. Only logged when it changes; never written.
        static uint32_t last_seen = 0xFFFFFFFFu;
        const uintptr_t menu = readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan);
        uint32_t now_value = 0;
        if (menu != 0 && SafeCopy(menu + 0x654C, &now_value, sizeof(now_value)) && now_value != last_seen) {
            Log("menu +0x654C = 0x%08X", now_value);
            last_seen = now_value;
        }
    }
    out.hp = v.hp;
    out.max_hp = v.max_hp;
    const bool steve_dead_now = v.max_hp > 0 && v.hp <= 0;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        out.selected_slot = g_melee.selectedSlot();
        for (int i = 0; i < MeleeController::kSlots; ++i) {
            out.hotbar[i] = static_cast<uint16_t>(g_melee.itemAt(i));
            out.hotbar_count[i] = static_cast<uint8_t>(std::min(255u, g_melee.countAt(i)));
        }
        out.absorption_mc = g_survival.absorptionMc();
        out.eating = g_eating.getProgress();
        steve.held_item = static_cast<uint16_t>(g_melee.heldItem());
        steve.cooldown = g_melee.cooldown();
        steve.eating = steve_dead_now ? 0.f : g_eating.getProgress();
        out.totem = g_feedback.totem();
        steve.swing = steve_dead_now ? 0.f : g_melee.swingProgress();
        out.hit = g_feedback.hit();
        out.hit_crit = g_feedback.crit();
        out.kill = g_feedback.kill();
        const mc::Inventory& inv = g_melee.inventory();
        for (int i = 0; i < mc::Inventory::kSlots; ++i) {
            out.inv_item[i] = static_cast<uint16_t>(inv.slot(i).item);
            out.inv_count[i] = static_cast<uint8_t>(std::min(255u, inv.slot(i).count));
        }
        out.cursor_item = static_cast<uint16_t>(inv.cursor().item);
        out.cursor_count = static_cast<uint8_t>(std::min(255u, inv.cursor().count));
    }
    out.inv_open = g_inv_open.load();
    out.mouse_x = g_inv_mx.load();
    out.mouse_y = g_inv_my.load();
    out.show = have_state && !ms.menu_focused && !ms.popup_open && !ls.screen_loading && fade < 0.02f;

    if (g_hide_native.load()) {
        const bool want_hidden = out.show && out.mc_mode;
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
    if (g_steve_enabled.load() && readCamera(g_reader, g_img.base, world, cam) &&
        detail::readPhysicsPosition(g_reader, g_img.base, static_cast<uintptr_t>(player), feet) &&
        detail::readPhysicsOrientation(g_reader, g_img.base, static_cast<uintptr_t>(player), q)) {
        steve.draw = true;
        steve.cam_valid = true;
        steve.cam.right = {cam.right[0], cam.right[1], cam.right[2]};
        steve.cam.up = {cam.up[0], cam.up[1], cam.up[2]};
        steve.cam.forward = {cam.forward[0], cam.forward[1], cam.forward[2]};
        steve.cam.position = {cam.position[0], cam.position[1], cam.position[2]};
        steve.fov_y = cam.fov_y;
        steve.feet[0] = feet[0];
        steve.feet[1] = feet[1];
        steve.feet[2] = feet[2];
        steve.yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]) + g_steve_yaw_offset;
    }
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        steve.hurt = g_feedback.hurt();
    }
    steve.dead = v.max_hp > 0 && v.hp <= 0;
    {
        static bool was_dead = false;
        if (steve.dead && !was_dead) eraudio::Play("entity.player.death");
        was_dead = steve.dead;
    }
    steve.first_person = g_first_person.load() && g_slot_probe.load() < 0;
    steve.speed_mps = g_walk_speed.load();
    steve.on_ground = g_on_ground.load();
    if (g_slot_probe.load() >= 0 || g_first_person.load()) steve.draw = false; // slot probing: show only the native model, with the one slot missing
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
                else if (key == "hide_native") g_hide_native.store(value != 0.f);
                else if (key == "fp_persist") g_fp_persist.store(value != 0.f);
                else if (key == "sound_volume") g_sound_volume.store(value);
                else if (key == "no_player_hit_vfx") g_no_player_hit_vfx.store(value != 0.f);
                else if (key == "first_person") g_first_person.store(value != 0.f);
                else if (key == "eye_height") g_eye_height.store(value);
                else if (key == "blocks") g_blocks_enabled.store(value != 0.f);
                else if (key == "block_collision") g_block_collision.store(value != 0.f);
                else if (key == "reach") g_reach.store(std::clamp(value, 1.f, 8.f));
                else if (key == "no_stagger") g_no_stagger.store(value != 0.f);
                else if (key == "inv_key") g_inv_vk.store(static_cast<int>(value));
                else if (key == "inv_sens") g_inv_sens.store(std::max(0.1f, value));
                else if (key == "kb_force") g_kb_force.store(value);
                else if (key == "hide_slots") g_hide_slots.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
                else if (key == "hide_mask1") g_hide_mask1.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
                else if (key == "hide_mask2") g_hide_mask2.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
            }
        }
        erov::SetSteveConfig(cfg);
        eraudio::Init((g_game_dir + "mods\\mc_adapter\\sounds\\").c_str(), g_sound_volume.load(), &Log);
        {
            std::vector<uint8_t> atlas;
            unsigned aw = 0, ah = 0;
            const std::string atlas_path = g_game_dir + "mods\\mc_adapter\\mc_hud_atlas.png";
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
        g_steve_enabled.store(true);
        Log("steve: hide_native=%d mask1=0x%X mask2=0x%X slots=0x%X", g_hide_native.load() ? 1 : 0, g_hide_mask1.load(), g_hide_mask2.load(),
        g_hide_slots.load());
        Log("steve: enabled (occlusion=%d depth_const=%.4f rel_bias=%.3f abs_bias=%.3f scene_height=%.0f yaw_offset=%.1f deg)",
            cfg.occlusion ? 1 : 0, cfg.depth_const, cfg.rel_bias, cfg.abs_bias, cfg.scene_height, g_steve_yaw_offset * 180.f / 3.14159265f);
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

void SetupInput() {
    if (!FileExists(g_game_dir + "mc_er_input.txt")) {
        Log("input: disabled (no mc_er_input.txt)");
        return;
    }
    g_input_enabled.store(true);
    g_click_attack.store(true);
    Log("input: mouse buttons are cleared at the DirectInput layer while MC mode is on and no game UI is open; left click attacks");
}

// ---- status loop ---------------------------------------------------------------------------------------------

void LogStatus() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    const uintptr_t menu = readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan);
    const uintptr_t loading = readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper);
    const uintptr_t fade = readSingleton(g_reader, g_img.base, g_rva_fade, sigs::kCSFade);
    char line[512];
    int n = snprintf(line, sizeof(line), "world=%p", reinterpret_cast<void*>(world));
    Vitals v;
    if (world != 0) {
        uint64_t player = 0;
        SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player));
        if (player != 0 && readVitals(g_reader, g_img.base, static_cast<uintptr_t>(player), v)) {
            n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " hp=%d/%d", v.hp, v.max_hp);
        }
    }
    MenuState ms;
    if (readMenuState(g_reader, menu, ms)) {
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " menu[a=0x%02X b=0x%02X popup=%d]", ms.layer_mask_a,
                      ms.layer_mask_b, ms.popup_open ? 1 : 0);
    }
    LoadingState ls;
    if (readLoadingState(g_reader, loading, ls)) {
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " load[job=%d screen=%d cnt=%d]", ls.job_active ? 1 : 0,
                      ls.screen_loading ? 1 : 0, ls.load_counter);
    }
    float fa = 0.f;
    if (readFadeAlpha(g_reader, fade, fa)) n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " fade=%.2f", fa);
    std::vector<EnemyInfo> list;
    if (world != 0 && enumerateEnemies(g_reader, g_img.base, list, 2000)) {
        int hostile = 0;
        float nearest = 1e9f;
        for (const EnemyInfo& e : list) {
            if (!e.hostile) continue;
            ++hostile;
            nearest = std::min(nearest, std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z));
        }
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " enemies=%zu hostile=%d nearest=%.1f", list.size(), hostile,
                      hostile > 0 ? nearest : -1.f);
    }
    if (g_input_enabled.load()) {
        const erin::Counters c = erin::TakeCounters();
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " input[mouse state=%u data=%u kbd state=%u data=%u other=%u cleared=%u]",
                      c.mouse_state, c.mouse_data, c.keyboard_state, c.keyboard_data, c.other, c.mouse_buttons_cleared);
    }
    (void)n;
    Log("%s", line);
}

DWORD WINAPI LoaderThread(LPVOID) {
    InitLogOnce();

    // Environment self-check: never touch a game that runs EasyAntiCheat.
    std::vector<std::string> names;
    HMODULE mods[1024];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        const size_t count = std::min<size_t>(needed / sizeof(HMODULE), 1024);
        for (size_t i = 0; i < count; ++i) {
            char name[MAX_PATH] = {};
            if (GetModuleFileNameA(mods[i], name, MAX_PATH)) {
                std::string s = name;
                names.push_back(s.substr(s.find_last_of("\\/") + 1));
            }
        }
    }
    const EnvVerdict verdict = checkEnvironment(names, FileExists(g_game_dir + "steam_appid.txt"));
    if (verdict != EnvVerdict::Ok) {
        Log("environment check FAILED (%s): not binding anything",
            verdict == EnvVerdict::EacLoaded ? "EAC module loaded" : "steam_appid.txt missing");
        return 0;
    }
    Log("environment check ok (%zu modules)", names.size());

    // Bind the singletons: wait for the code to be readable and the signatures to be unique.
    for (int attempt = 0;; ++attempt) {
        if (ReadImageInfo(g_img)) {
            const Bound w = LocateSingleton(g_img, sigs::kWorldChrMan);
            const Bound m = LocateSingleton(g_img, sigs::kCSMenuMan);
            const Bound l = LocateSingleton(g_img, sigs::kCSNowLoadingHelper);
            const Bound f = LocateSingleton(g_img, sigs::kCSFade);
            if (w.ok && m.ok && l.ok && f.ok) {
                g_rva_world = w.rva;
                g_rva_menu = m.rva;
                g_rva_loading = l.rva;
                g_rva_fade = f.rva;
                Log("singletons bound: WorldChrMan=0x%X CSMenuMan=0x%X CSNowLoadingHelper=0x%X CSFade=0x%X (image base %p)", w.rva,
                    m.rva, l.rva, f.rva, reinterpret_cast<void*>(g_img.base));
                break;
            }
            if (attempt % 5 == 0) {
                Log("waiting for signatures (world=%d menu=%d loading=%d fade=%d)", w.ok, m.ok, l.ok, f.ok);
            }
        }
        if (attempt > 120) {
            Log("giving up: singleton signatures never became unique");
            return 0;
        }
        Sleep(1000);
    }

    SetupDamage();
    SetupOverlay();
    SetupInput();
    CreateThread(nullptr, 0, KeyThread, nullptr, 0, nullptr);

    for (;;) {
        Sleep(1000);
        if (g_game_tid.load() == 0) {
            const DWORD tid = FindGameWindowThread();
            if (tid != 0) {
                g_game_tid.store(tid);
                Log("game thread = %lu (owner of the game window)", tid);
            }
        }
        LogStatus();
    }
}

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
DirectInput8Create_t g_system_create = nullptr;
std::atomic<bool> g_wants_input_hooks{false};

} // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    if (g_system_create == nullptr) {
        char dir[MAX_PATH] = {};
        if (GetSystemDirectoryA(dir, MAX_PATH) > 0) {
            const std::string path = std::string(dir) + "\\dinput8.dll";
            if (HMODULE m = LoadLibraryA(path.c_str())) {
                g_system_create = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(m, "DirectInput8Create"));
            }
        }
    }
    if (g_system_create == nullptr) return E_FAIL;
    static std::once_flag decided;
    std::call_once(decided, [] {
        InitLogOnce();
        const DWORD a = GetFileAttributesA((g_game_dir + "mc_er_input.txt").c_str());
        if (a != INVALID_FILE_ATTRIBUTES && EnsureMinHook()) {
            erin::SetLog([](const char* fmt, ...) {
                char msg[512];
                va_list args;
                va_start(args, fmt);
                vsnprintf(msg, sizeof(msg), fmt, args);
                va_end(args);
                Log("%s", msg);
            });
            g_wants_input_hooks.store(true);
        }
    });
    const HRESULT hr = g_system_create(hinst, version, riid, out, outer);
    if (SUCCEEDED(hr) && out != nullptr && *out != nullptr && g_wants_input_hooks.load()) erin::OnDirectInputCreated(riid, *out);
    return hr;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, LoaderThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
