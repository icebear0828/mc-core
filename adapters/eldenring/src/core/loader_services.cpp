#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

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

bool EnsureMinHook() {
    return eldenring::hooks::GameHooks::Instance().initMinHook();
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
    SetupCreativeProbes();
    SetupBulletLog();
    SetupOverlay();
    SetupZoneProbe();
    SetupInput();
    CreateThread(nullptr, 0, KeyThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, CrackTunerThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, BuddyThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, AnimDebugThread, nullptr, 0, nullptr);

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

// Phase A: debug triggers for the elytra/trident animation slice. Off unless
// mc_er_dbg_anim.txt exists, so a normal install never sees these keys.
DWORD WINAPI AnimDebugThread(LPVOID) {
    if (!FileExists(g_game_dir + "mc_er_dbg_anim.txt")) return 0;
    Log("anim-debug: Insert=glide Delete=riptide Home=trident-charge End=firework");
    auto& m = eldenring::RuntimeContext::Instance().mode();
    bool p_ins = false, p_del = false, p_home = false, p_end = false;
    for (;;) {
        const bool fg = GameInForeground();
        auto edge = [&](int vk, bool& prev) {
            const bool down = fg && ((GetAsyncKeyState(vk) & 0x8000) != 0);
            const bool hit = down && !prev;
            prev = down;
            return hit;
        };
        if (edge(VK_INSERT, p_ins)) {
            const bool v = !m.dbg_glide.load();
            m.dbg_glide.store(v);
            Log("anim-debug: glide %s", v ? "on" : "off");
        }
        if (edge(VK_DELETE, p_del)) {
            const bool v = !m.dbg_riptide.load();
            m.dbg_riptide.store(v);
            Log("anim-debug: riptide %s", v ? "on" : "off");
        }
        if (edge(VK_HOME, p_home)) {
            const float v = m.dbg_trident_charge.load() > 0.5f ? 0.f : 1.f;
            m.dbg_trident_charge.store(v);
            Log("anim-debug: trident charge %.2f", v);
        }
        if (edge(VK_END, p_end)) {
            m.dbg_firework_seq.fetch_add(1);
            Log("anim-debug: firework");
        }
        Sleep(30);
    }
    return 0;
}

DWORD WINAPI KeyThread(LPVOID) {
    Log("key: KeyThread started (tid %lu)", GetCurrentThreadId());
    eldenring::input::HotkeyDispatcher::Instance().runKeyLoop();
    return 0;
}

DWORD WINAPI CrackTunerThread(LPVOID) {
    bool prev_down = false, prev_up = false, prev_fine_down = false, prev_fine_up = false;
    auto adjust = [](float delta, const char* kind) {
        const float v = std::clamp(g_ground_crack_lift.load(std::memory_order_relaxed) + delta, -2.0f, 1.0f);
        g_ground_crack_lift.store(v, std::memory_order_relaxed);
        Log("cracktune: lift=%.2f m (%s)", v, kind);
    };
    for (;;) {
        const bool focused = GameInForeground();
        const bool down = focused && (((GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0) || ((GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0));
        const bool up = focused && (((GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0) || ((GetAsyncKeyState(VK_NEXT) & 0x8000) != 0));
        const bool fine_down = focused && ((GetAsyncKeyState(VK_OEM_COMMA) & 0x8000) != 0);
        const bool fine_up = focused && ((GetAsyncKeyState(VK_OEM_PERIOD) & 0x8000) != 0);
        if (down && !prev_down) adjust(-0.05f, "[ / PageDown, coarse");
        if (up && !prev_up) adjust(+0.05f, "] / PageUp, coarse");
        if (fine_down && !prev_fine_down) adjust(-0.01f, ", fine");
        if (fine_up && !prev_fine_up) adjust(+0.01f, ". fine");
        prev_down = down;
        prev_up = up;
        prev_fine_down = fine_down;
        prev_fine_up = fine_up;
        Sleep(30);
    }
    return 0;
}

// Forwarded by the exported DirectInput8Create in loader.cpp. Kept here so the
// entry point stays a few lines and the proxy state lives with the services.
HRESULT WINAPI DirectInput8CreateImpl(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
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

} // namespace eldenring::loader_detail
