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
#include <vector>

#include "eldenring_damage.hpp"
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

DWORD WINAPI KeyThread(LPVOID) {
    bool prev = false;
    for (;;) {
        Sleep(15);
        const bool down = GameInForeground() && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        if (down && !prev && g_damage_enabled.load()) EnqueueNearestHostile();
        prev = down;
    }
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
    if (MH_Initialize() != MH_OK) {
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
    CreateThread(nullptr, 0, KeyThread, nullptr, 0, nullptr);
    Log("damage: enabled; ClampHP hooked at %p (RVA 0x%llX); press F8 to hit the nearest hostile enemy (require_victim_updating=%d)", reinterpret_cast<void*>(clamp),
        static_cast<unsigned long long>(clamp - g_img.base), g_require_victim_updating.load() ? 1 : 0);
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
    (void)n;
    Log("%s", line);
}

DWORD WINAPI LoaderThread(LPVOID) {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    g_game_dir = exe;
    g_game_dir.resize(g_game_dir.find_last_of("\\/") + 1);
    g_start_ms = GetTickCount64();
    // _fsopen with _SH_DENYNO: other processes (the probes, `type`) can read the log while the game runs.
    g_log = _fsopen((g_game_dir + "mc_er.log").c_str(), "a", _SH_DENYNO);
    Log("==== eldenring adapter loaded (pid %lu, exe %s) ====", GetCurrentProcessId(), exe);

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
    return g_system_create(hinst, version, riid, out, outer);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, LoaderThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
