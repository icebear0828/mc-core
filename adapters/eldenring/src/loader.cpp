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

#include "input_hook.hpp"
#include "overlay_d3d12.hpp"
#include "eldenring_camera.hpp"
#include "eldenring_damage.hpp"
#include "eldenring_model.hpp"
#include "eldenring_pick.hpp"
#include "eldenring_steve.hpp"
#include "eldenring_singletons.hpp"
#include "eldenring_state.hpp"
#include "eldenring_world.hpp"

using namespace eldenring::live;

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
std::atomic<int> g_selected_slot{0};
std::atomic<bool> g_steve_enabled{false};
std::atomic<bool> g_hide_native{false};
std::unordered_map<uintptr_t, uint32_t> g_hidden_flags; // flag word addresses we cleared -> their original value
std::atomic<uint32_t> g_hide_mask1{layout::kDispVisibleBit}; // bits cleared in disp_flags1 (+0x20)
std::atomic<uint32_t> g_hide_mask2{0};                       // bits cleared in disp_flags2 (+0x24)
float g_steve_yaw_offset = 0.f;
std::atomic<bool> g_require_victim_updating{true};
DamageQueue g_queue;
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
    }
    return "?";
}

int32_t HpOf(uintptr_t chr) {
    Vitals v;
    return readVitals(g_reader, g_img.base, chr, v) ? v.hp : -1;
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
    c.max_per_drain = 1;
    c.expire_after_ticks = 60 * 5;
    c.invoke = [](uintptr_t dmg, uintptr_t attacker, void* ctx) {
        uint64_t owner = 0;
        SafeCopy(dmg + layout::kOwnerInDataModule, &owner, sizeof(owner));
        const int32_t before = HpOf(static_cast<uintptr_t>(owner));
        const bool ok = CallVfunc7(dmg, attacker, ctx);
        const int32_t after = HpOf(static_cast<uintptr_t>(owner));
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
    for (const DamageResult& r : g_queue.drain(c)) {
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
void ClickAttack() {
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
    const bool ok = g_queue.enqueue(e.chr, 50, NowTick());
    Log("click: queued 50 on chr=%p npc=%d hp=%d (%s)", reinterpret_cast<void*>(e.chr), e.npc_id, e.hp, ok ? "ok" : "queue full");
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

DWORD WINAPI KeyThread(LPVOID) {
    bool prev8 = false, prev6 = false, prev7 = false;
    for (;;) {
        Sleep(15);
        const bool fg = GameInForeground();
        if (g_input_enabled.load()) erin::SetSuppressMouseButtons(fg && WantSuppress());
        if (g_input_enabled.load() && erin::TakeLeftClick() && fg && g_click_attack.load() && g_damage_enabled.load() && WantSuppress()) {
            ClickAttack();
        }
        const bool d8 = fg && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        const bool d6 = fg && (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        if (d8 && !prev8 && g_damage_enabled.load()) EnqueueNearestHostile();
        if (d6 && !prev6) {
            g_mc_mode.store(!g_mc_mode.load());
            Log("F6: MC mode %s", g_mc_mode.load() ? "on" : "off");
        }
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
void UpdateNativeModel(uintptr_t player, bool hide) {
    if (!hide && g_hidden_flags.empty()) return;
    const std::vector<uintptr_t> addrs = collectDispFlagAddresses(g_reader, g_img.base, player);
    const uint32_t masks[2] = {g_hide_mask1.load(), g_hide_mask2.load()};
    for (uintptr_t base : addrs) {
        for (unsigned w = 0; w < 2; ++w) {
            const uintptr_t a = base + w * (layout::kDispFlags2 - layout::kDispFlags1);
            if (masks[w] == 0) continue;
            uint32_t flags = 0;
            if (!SafeCopy(a, &flags, sizeof(flags))) continue;
            if (hide) {
                if ((flags & masks[w]) != 0) {
                    g_hidden_flags.emplace(a, flags);
                    SafeWrite32(a, hideBits(flags, masks[w]));
                }
            } else {
                const auto it = g_hidden_flags.find(a);
                if (it != g_hidden_flags.end()) SafeWrite32(a, restoreBits(flags, it->second, masks[w]));
            }
        }
    }
    if (!hide) g_hidden_flags.clear();
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
    out.hp = v.hp;
    out.max_hp = v.max_hp;
    out.selected_slot = g_selected_slot.load();
    out.show = have_state && !ms.menu_focused && !ms.popup_open && !ls.screen_loading && fade < 0.02f;

    if (g_hide_native.load()) UpdateNativeModel(static_cast<uintptr_t>(player), out.show && out.mc_mode);

    steve.draw = false;
    CameraPose cam;
    float feet[3], q[4];
    if (g_steve_enabled.load() && readCamera(g_reader, g_img.base, world, cam) &&
        detail::readPhysicsPosition(g_reader, g_img.base, static_cast<uintptr_t>(player), feet) &&
        detail::readPhysicsOrientation(g_reader, g_img.base, static_cast<uintptr_t>(player), q)) {
        steve.draw = true;
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
                else if (key == "hide_mask1") g_hide_mask1.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
                else if (key == "hide_mask2") g_hide_mask2.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
            }
        }
        erov::SetSteveConfig(cfg);
        g_steve_enabled.store(true);
        Log("steve: hide_native=%d mask1=0x%X mask2=0x%X", g_hide_native.load() ? 1 : 0, g_hide_mask1.load(), g_hide_mask2.load());
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
