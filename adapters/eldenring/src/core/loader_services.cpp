#include "core/loader_internal.hpp"
#include "eldenring_config.hpp"
#include "eldenring_physprobe.hpp"
#include "eldenring_physwrite.hpp"
#include "mc/hand_items.hpp"

#include <cstdlib>
#include <share.h>

namespace eldenring::loader_detail {

namespace {

FILE* g_physprobe_file = nullptr;
bool g_physprobe_on = false;
uint64_t g_physprobe_next_check_ms = 0;
eldenring::physprobe::State g_physprobe_state;
eldenring::physprobe::Config g_physprobe_config;
std::atomic<int> g_physwrite_request{-1};
eldenring::physwrite::HoldState g_physwrite_hold;
uint64_t g_physwrite_hold_ms = 1000;
bool g_physwrite_full = false;
bool g_physwrite_glide = false;
float g_physwrite_speed_mps = 10.f;
uint64_t g_physwrite_hold_log_ms = 0;

void ClosePhysProbe() {
    if (g_physprobe_file != nullptr) {
        fprintf(g_physprobe_file, "=== physprobe stop ===\n");
        fflush(g_physprobe_file);
        fclose(g_physprobe_file);
        g_physprobe_file = nullptr;
    }
    g_physprobe_on = false;
}

void OpenPhysProbe(const std::string& config_path) {
    const std::string log_path = g_game_dir + "mc_er_physlog.txt";
    // _SH_DENYNO: the trace can be tailed while the game runs, like mc_er.log.
    FILE* f = _fsopen(log_path.c_str(), "a", _SH_DENYNO);
    if (f == nullptr) {
        Log("physprobe: cannot open %s", log_path.c_str());
        return;
    }

    eldenring::physprobe::Config cfg;
    std::vector<uint8_t> bytes;
    if (ReadFileAll(config_path, bytes)) {
        const std::string text(bytes.begin(), bytes.end());
        const auto at = text.find("interval_ms=");
        if (at != std::string::npos) {
            const int v = std::atoi(text.c_str() + at + 12);
            if (v >= 10 && v <= 1000) cfg.interval_ms = static_cast<uint64_t>(v);
        }
    }

    g_physprobe_config = cfg;
    g_physprobe_state = {};
    g_physprobe_file = f;
    g_physprobe_on = true;
    fprintf(g_physprobe_file, "=== physprobe start interval_ms=%llu ===\n",
                 static_cast<unsigned long long>(cfg.interval_ms));
    fflush(g_physprobe_file);
    Log("physprobe: enabled, log=%s interval=%llu ms", log_path.c_str(),
        static_cast<unsigned long long>(cfg.interval_ms));
}

void LoadPhysWriteConfig(const std::string& path) {
    g_physwrite_hold_ms = 1000;
    g_physwrite_full = false;
    g_physwrite_glide = false;
    g_physwrite_speed_mps = 10.f;
    std::vector<uint8_t> bytes;
    if (!ReadFileAll(path, bytes)) return;
    const std::string text(bytes.begin(), bytes.end());
    const auto hold = text.find("hold_ms=");
    if (hold != std::string::npos) {
        const long v = std::atol(text.c_str() + hold + 8);
        if (v >= 0 && v <= 5000) g_physwrite_hold_ms = static_cast<uint64_t>(v);
    }
    const auto full = text.find("full=");
    if (full != std::string::npos) g_physwrite_full = std::atoi(text.c_str() + full + 5) != 0;
    const auto glide = text.find("glide=");
    if (glide != std::string::npos) g_physwrite_glide = std::atoi(text.c_str() + glide + 6) != 0;
    const auto speed = text.find("speed_mps=");
    if (speed != std::string::npos) {
        const float v = static_cast<float>(std::atof(text.c_str() + speed + 10));
        if (v > 0.f && v <= 100.f) g_physwrite_speed_mps = v;
    }
}

// Write the target and request a proxy sync. `full` also mimics the game's own
// set-position: +0x90 = 0x0101 and +0x1DE = 1.
bool WritePosition(uintptr_t module, const float target[3], bool full, bool& synced) {
    const bool wrote = WriteBytesSafe(module + layout::kPhysicsPosition, target, sizeof(float) * 3) &&
                       WriteBytesSafe(module + layout::kPhysicsPosition + 0x10, target, sizeof(float) * 3);
    if (full) {
        const uint16_t word = 0x0101;
        WriteBytesSafe(module + 0x90, &word, sizeof(word));
        const uint8_t one = 1;
        WriteBytesSafe(module + 0x1DE, &one, 1);
    }
    const uint8_t one = 1;
    synced = WriteBytesSafe(module + layout::kPhysicsProxySyncRequest, &one, 1);
    return wrote;
}

} // namespace
// (optional `interval_ms=<10..1000>`); writes mc_er_physlog.txt. It only calls
// the existing read paths and never writes to the game.
void PhysProbeStep(float dt) {
    const uint64_t now = GetTickCount64();
    if (now >= g_physprobe_next_check_ms) {
        g_physprobe_next_check_ms = now + 1000;
        const std::string config_path = g_game_dir + "mc_er_physprobe.txt";
        const bool exists = FileExists(config_path);
        if (exists && !g_physprobe_on) {
            OpenPhysProbe(config_path);
        } else if (!exists && g_physprobe_on) {
            ClosePhysProbe();
        }
    }

    if (!g_physprobe_on || g_physprobe_file == nullptr) return;
    if (!GameInForeground()) return;
    if (!eldenring::physprobe::shouldSample(g_physprobe_state, now, g_physprobe_config)) return;

    const uintptr_t player = PlayerChrPtr();
    if (player == 0) return;
    const uintptr_t module = detail::readPhysicsModule(g_reader, g_img.base, player);
    if (module == 0) return;

    eldenring::physprobe::Sample s;
    s.now_ms = now;
    s.dt = dt;
    s.have_physics = true;
    if (!SafeCopy(module + layout::kPhysicsPosition, s.pos, sizeof(s.pos))) return;
    SafeCopy(module + 0x80, s.last_pos, sizeof(s.last_pos));
    SafeCopy(module + layout::kPhysicsOrientation, s.orientation, sizeof(s.orientation));
    SafeCopy(module + 0x120, s.v120, sizeof(s.v120));
    SafeCopy(module + 0x1B8, &s.v1b8, sizeof(s.v1b8));
    SafeCopy(module + 0x90, &s.word90, sizeof(s.word90));
    SafeCopy(module + 0x91, &s.b91, 1);
    SafeCopy(module + 0x92, &s.b92, 1);
    SafeCopy(module + 0x93, &s.b93, 1);
    SafeCopy(module + 0x1D0, &s.b1d0, 1);
    SafeCopy(module + 0x1D1, &s.b1d1, 1);
    SafeCopy(module + 0x1D5, &s.b1d5, 1);
    SafeCopy(module + 0xCC, &s.bcc, 1);

    Vitals v;
    if (readVitals(g_reader, g_img.base, player, v)) {
        s.have_vitals = true;
        s.hp = v.hp;
        s.max_hp = v.max_hp;
    }
    s.fall_timer = FallTimer(player);

    CameraPose cam{};
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    if (world != 0 && readCamera(g_reader, g_img.base, world, cam)) {
        s.have_camera = true;
        std::memcpy(s.cam_pos, cam.position, sizeof(s.cam_pos));
        std::memcpy(s.cam_fwd, cam.forward, sizeof(s.cam_fwd));
        std::memcpy(s.cam_up, cam.up, sizeof(s.cam_up));
        std::memcpy(s.cam_right, cam.right, sizeof(s.cam_right));
        s.fov_y = cam.fov_y;
    }

    const auto event = eldenring::physprobe::detectEvent(g_physprobe_state, s);
    if (event.any()) {
        const std::string line = eldenring::physprobe::formatEvent(event, s);
        fprintf(g_physprobe_file, "%s\n", line.c_str());
    }
    const std::string line = eldenring::physprobe::formatSample(s);
    fprintf(g_physprobe_file, "%s\n", line.c_str());
    fflush(g_physprobe_file);
    eldenring::physprobe::commit(g_physprobe_state, s);
}

// Controlled teleport test. The key thread only posts a request; the game
// thread performs the writes here, so the engine never sees a write from the
// input thread. A one-shot write is reverted by the character controller on
// the next frame; hold_ms > 0 keeps writing the target every frame.
void PhysWriteTestStep() {
    const uint64_t now = GetTickCount64();

    if (g_physwrite_hold.active) {
        if (RuntimeContext::Instance().mode().elytra_gliding.load(std::memory_order_relaxed)) {
            Log("physwrite: hold released (elytra gliding)");
            eldenring::physwrite::stopHold(g_physwrite_hold);
            return;
        }
        if (PlayerDead()) {
            Log("physwrite: hold released (player dead)");
            eldenring::physwrite::stopHold(g_physwrite_hold);
            return;
        }
        if (!eldenring::physwrite::holdActive(g_physwrite_hold, now)) {
            Log("physwrite: hold released");
            eldenring::physwrite::stopHold(g_physwrite_hold);
        } else {
            const uintptr_t player = PlayerChrPtr();
            const uintptr_t module = player != 0 ? detail::readPhysicsModule(g_reader, g_img.base, player) : 0;
            if (module != 0) {
                if (g_physwrite_hold.glide) {
                    CameraPose cam{};
                    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
                    if (world != 0 && readCamera(g_reader, g_img.base, world, cam)) {
                        eldenring::physwrite::stepGlide(g_physwrite_hold, cam.forward, now);
                    }
                }
                bool synced = false;
                const bool wrote = WritePosition(module, g_physwrite_hold.target, g_physwrite_full, synced);
                if (now - g_physwrite_hold_log_ms >= 250) {
                    g_physwrite_hold_log_ms = now;
                    float read_back[3] = {};
                    SafeCopy(module + layout::kPhysicsPosition, read_back, sizeof(read_back));
                    uint8_t ground = 0;
                    uint8_t falling = 0;
                    SafeCopy(module + layout::kStandingOnGroundInPhysics, &ground, 1);
                    SafeCopy(module + 0x1D0, &falling, 1);
                    Log("physwrite: hold wrote=%d sync=%d target=(%.3f %.3f %.3f) read=(%.3f %.3f %.3f) ground=%u falling=%u fall_t=%.3f",
                        wrote ? 1 : 0, synced ? 1 : 0,
                        static_cast<double>(g_physwrite_hold.target[0]),
                        static_cast<double>(g_physwrite_hold.target[1]),
                        static_cast<double>(g_physwrite_hold.target[2]),
                        static_cast<double>(read_back[0]), static_cast<double>(read_back[1]),
                        static_cast<double>(read_back[2]), static_cast<unsigned>(ground),
                        static_cast<unsigned>(falling), static_cast<double>(FallTimer(player)));
                }
            }
            return;
        }
    }

    const int index = g_physwrite_request.exchange(-1, std::memory_order_relaxed);
    if (index < 0) return;
    if (!eldenring::physwrite::validIndex(index)) return;
    if (!FileExists(g_game_dir + "mc_er_physwritetest.txt")) {
        Log("physwrite: ignored (mc_er_physwritetest.txt missing)");
        return;
    }
    if (!GameInForeground()) {
        Log("physwrite: skipped (not foreground)");
        return;
    }
    if (PlayerDead()) {
        Log("physwrite: skipped (player dead)");
        return;
    }

    const uintptr_t player = PlayerChrPtr();
    const uintptr_t module = player != 0 ? detail::readPhysicsModule(g_reader, g_img.base, player) : 0;
    if (module == 0) {
        Log("physwrite: skipped (no physics module)");
        return;
    }

    float before[3] = {};
    if (!SafeCopy(module + layout::kPhysicsPosition, before, sizeof(before))) {
        Log("physwrite: skipped (cannot read position)");
        return;
    }

    if (g_physwrite_hold_ms > 0) {
        eldenring::physwrite::startHold(g_physwrite_hold, before, eldenring::physwrite::kHeightsM[index], now,
                                        g_physwrite_hold_ms);
        g_physwrite_hold.glide = g_physwrite_glide;
        g_physwrite_hold.speed_mps = g_physwrite_speed_mps;
        g_physwrite_hold_log_ms = 0;
        bool synced = false;
        const bool wrote = WritePosition(module, g_physwrite_hold.target, g_physwrite_full, synced);
        Log("physwrite: hold start key=%d +%.0f m for %llu ms wrote=%d sync=%d target=(%.3f %.3f %.3f)",
            index, static_cast<double>(eldenring::physwrite::kHeightsM[index]),
            static_cast<unsigned long long>(g_physwrite_hold_ms), wrote ? 1 : 0, synced ? 1 : 0,
            static_cast<double>(g_physwrite_hold.target[0]),
            static_cast<double>(g_physwrite_hold.target[1]),
            static_cast<double>(g_physwrite_hold.target[2]));
        return;
    }

    const float after[3] = {before[0], before[1] + eldenring::physwrite::kHeightsM[index], before[2]};
    bool synced = false;
    const bool wrote = WritePosition(module, after, g_physwrite_full, synced);

    float read_back[3] = {};
    SafeCopy(module + layout::kPhysicsPosition, read_back, sizeof(read_back));
    uint8_t ground = 0;
    uint8_t falling = 0;
    SafeCopy(module + layout::kStandingOnGroundInPhysics, &ground, 1);
    SafeCopy(module + 0x1D0, &falling, 1);
    const float fall_t = FallTimer(player);
    Vitals v;
    const bool have_vitals = readVitals(g_reader, g_img.base, player, v);
    Log("%s", eldenring::physwrite::formatResult(index, before, after, read_back, wrote, synced,
                                                 static_cast<int>(ground), static_cast<int>(falling), fall_t,
                                                 have_vitals ? v.hp : -1, have_vitals ? v.max_hp : -1)
                  .c_str());
}

bool TryUseElytraFirework(mc::Hand hand) {
    auto& ctx = RuntimeContext::Instance();
    if (!ctx.mode().mc_mode.load(std::memory_order_relaxed) ||
        !ctx.mode().elytra_gliding.load(std::memory_order_relaxed)) {
        return false;
    }
    std::lock_guard<std::mutex> g(g_melee_mutex);
    const mc::ItemId item = hand == mc::Hand::Main ? g_melee.heldItem() : g_melee.offhandItem();
    const unsigned count = hand == mc::Hand::Main ? g_melee.countAt(g_melee.selectedSlot()) : g_melee.offhandCount();
    if (count == 0 || item != mc::ItemId::FireworkRocket) return false;
    // Vanilla: 10*(1+flight_duration)+rand(6)+rand(7). We do not track the
    // component's flight_duration yet; 30 ticks matches the common duration 1.
    const int ticks = 30;
    if (!eldenring::movement::PlayerLocomotion::Instance().useElytraFirework(ticks)) return false;
    if (eldenring::creative::consumesItems(ctx.currentGameMode())) {
        if (hand == mc::Hand::Main) {
            g_melee.consumeAt(g_melee.selectedSlot());
        } else {
            g_melee.consumeOffhand(mc::ItemId::FireworkRocket);
        }
    }
    Log("elytra: firework boost %d ticks hand=%s", ticks, hand == mc::Hand::Main ? "main" : "off");
    return true;
}

bool TryThrowEnderPearl(mc::Hand hand) {
    auto& ctx = RuntimeContext::Instance();
    if (!ctx.mode().mc_mode.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> g(g_melee_mutex);
    const mc::ItemId item = hand == mc::Hand::Main ? g_melee.heldItem() : g_melee.offhandItem();
    const unsigned count = hand == mc::Hand::Main ? g_melee.countAt(g_melee.selectedSlot()) : g_melee.offhandCount();
    if (count == 0 || item != mc::ItemId::EnderPearl) return false;
    // The launch happens on the next camera step, where the owner's velocity and the view are readable.
    eldenring::movement::PlayerLocomotion::Instance().requestEnderPearlThrow();
    if (eldenring::creative::consumesItems(ctx.currentGameMode())) {
        if (hand == mc::Hand::Main) {
            g_melee.consumeAt(g_melee.selectedSlot());
        } else {
            g_melee.consumeOffhand(mc::ItemId::EnderPearl);
        }
    }
    Log("pearl: throw requested hand=%s", hand == mc::Hand::Main ? "main" : "off");
    return true;
}

bool TryStartRiptide(mc::Hand hand) {
    auto& ctx = RuntimeContext::Instance();
    if (!ctx.mode().mc_mode.load(std::memory_order_relaxed)) return false;
    if (ctx.mode().riptide_require_water.load(std::memory_order_relaxed)) {
        // The adapter has no water/rain read yet, so the vanilla gate cannot be satisfied.
        Log("riptide: require_water=1 but water/rain detection is unavailable; release the gate to use it");
        return false;
    }
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        const mc::ItemId item = hand == mc::Hand::Main ? g_melee.heldItem() : g_melee.offhandItem();
        const unsigned count = hand == mc::Hand::Main ? g_melee.countAt(g_melee.selectedSlot()) : g_melee.offhandCount();
        if (count == 0 || item != mc::ItemId::Trident) return false;
    }
    const bool off_trident = hand == mc::Hand::Off;
    ctx.mode().trident_offhand.store(off_trident, std::memory_order_relaxed);
    auto& loco = eldenring::movement::PlayerLocomotion::Instance();
    // Gliding no longer turns the release into a throw: the dash pauses the glide and resumes it at the new
    // position (PlayerLocomotion::stepRiptide's riptide_was_gliding_).
    const int level = std::clamp(ctx.mode().riptide_level.load(std::memory_order_relaxed), 1, 3);
    if (!loco.startRiptide(level)) return false;
    Log("riptide: requested level=%d%s", level, off_trident ? " (offhand)" : "");
    return true;
}

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

// The adapter's configuration in the host runtime's portable shape. Called by
// LoaderThread after the environment gate, so a refused host never reads
// mc_er.ini and never starts the hook engine.
mc::host::HostConfig LoadHostConfig() {
    // InitLogOnce resolves g_game_dir from the module path. It is idempotent,
    // so the runtime and LoaderThread may call it again.
    InitLogOnce();
    mc::host::HostConfig config;
    config.game_dir = g_game_dir;
    eldenring::AdapterConfig& adapter = eldenring::AdapterConfig::Instance();
    adapter.EnsureLoaded(config.game_dir);
    config.enable_overlay = adapter.FeatureEnabled(eldenring::ConfigFeature::Overlay);
    config.enable_input = adapter.FeatureEnabled(eldenring::ConfigFeature::Input);
    // Damage and bullet-fire are Elden Ring gameplay switches; the adapter reads
    // them from AdapterConfig where it needs them, not through the host seam.
    return config;
}

DWORD WINAPI LoaderThread(LPVOID param) {
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

    // The environment gate has passed: configuration, logging and the hook
    // engine may now come up, in that order.
    auto* runtime = static_cast<mc::host::IHostRuntime*>(param);
    if (runtime == nullptr) {
        Log("loader: no host runtime was supplied");
        return 0;
    }
    if (!runtime->initialize(LoadHostConfig())) {
        Log("loader: the host runtime refused to initialize");
        return 0;
    }
    {
        // Register the input notification before the signature scan: the game
        // may create its DirectInput device while we are still binding.
        mc::host::InputCallbacks input;
        input.on_direct_input_created = &erin::OnDirectInputCreated;
        if (!runtime->installInputHooks(input)) {
            // Input is disabled or the hook engine refused: release any
            // interface the proxy kept for a late registration.
            UninstallInputHooks();
        }
    }

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
    {
        // SetupOverlay has loaded the assets and the Steve config; the runtime
        // now owns the detour lifecycle and the per-frame notification.
        mc::host::PresentCallbacks present;
        present.on_present = &erov::HostPresentTick;
        (void)runtime->installPresentHooks(present);
    }
    SetupZoneProbe();
    SetupInput();
    g_elytra_ignore_focus = FileExists(g_game_dir + "mc_er_elytra_test.txt");
    if (g_elytra_ignore_focus) Log("elytra: test mode, foreground check disabled");
    LoadInventorySave();
    CreateThread(nullptr, 0, InventorySaveThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, KeyThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, CrackTunerThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, BuddyThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, AnimDebugThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, PhysWriteTestThread, nullptr, 0, nullptr);

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

// Debug teleport keys. Off unless mc_er_physwritetest.txt exists at launch.
DWORD WINAPI PhysWriteTestThread(LPVOID) {
    const std::string config_path = g_game_dir + "mc_er_physwritetest.txt";
    if (!FileExists(config_path)) return 0;
    LoadPhysWriteConfig(config_path);
    Log("physwrite: Numpad0=+1m Numpad1=+5m Numpad2=+10m Numpad3=+20m hold_ms=%llu full=%d glide=%d speed=%.1f m/s",
        static_cast<unsigned long long>(g_physwrite_hold_ms), g_physwrite_full ? 1 : 0, g_physwrite_glide ? 1 : 0,
        static_cast<double>(g_physwrite_speed_mps));
    bool prev[eldenring::physwrite::kHeightCount] = {};
    for (;;) {
        const bool fg = GameInForeground();
        for (int i = 0; i < eldenring::physwrite::kHeightCount; ++i) {
            const bool down = fg && ((GetAsyncKeyState(VK_NUMPAD0 + i) & 0x8000) != 0);
            if (down && !prev[i]) {
                g_physwrite_request.store(i, std::memory_order_relaxed);
                Log("%s", eldenring::physwrite::formatRequest(i).c_str());
            }
            prev[i] = down;
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

namespace {

// The runtime's input notification. DirectInput8Create runs on the game thread
// while the loader thread registers the callback, so the callback and the
// interfaces that appeared before it are behind a mutex.
std::mutex g_input_mutex;
std::function<void(void*)> g_input_callback;
std::vector<void*> g_pending_input_ifaces;
// The last IDirectInput8, kept AddRef'd so a later install can re-hook
// CreateDevice without waiting for the game to create the interface again.
void* g_known_input_iface = nullptr;
// False after UninstallInputHooks(): stop queuing interfaces that nothing will hook.
bool g_input_accepting = true;

using ComAddRefFn = ULONG(STDMETHODCALLTYPE*)(void*);
using ComReleaseFn = ULONG(STDMETHODCALLTYPE*)(void*);

ULONG ComAddRef(void* iface) {
    void** vtable = *reinterpret_cast<void***>(iface);
    return reinterpret_cast<ComAddRefFn>(vtable[1])(iface);
}

void ComRelease(void* iface) {
    void** vtable = *reinterpret_cast<void***>(iface);
    reinterpret_cast<ComReleaseFn>(vtable[2])(iface);
}

} // namespace

bool InstallInputHooks(const mc::host::InputCallbacks& callbacks) {
    // diagnostics::Log already matches erin::LogFn; no per-subsystem wrapper needed.
    erin::SetLog(&eldenring::diagnostics::Log);
    erin::SetEnabled(true);
    std::vector<void*> release_later;
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_input_callback = callbacks.on_direct_input_created;
        if (!g_input_callback) return true;
        g_input_accepting = true;
        std::vector<void*> pending;
        pending.swap(g_pending_input_ifaces);
        if (!pending.empty()) {
            Log("input: %zu DirectInput interface(s) appeared before the input hooks were installed; hooking them late "
                "(devices created before this point are not covered)",
                pending.size());
            if (g_known_input_iface != nullptr) ComRelease(g_known_input_iface);
            // Keep the first one so a later install can re-hook CreateDevice.
            g_known_input_iface = pending.front();
            for (void* iface : pending) g_input_callback(iface);
            release_later.assign(pending.begin() + 1, pending.end());
        } else if (g_known_input_iface != nullptr) {
            // Install after Uninstall(): re-hook CreateDevice on the interface we
            // kept; RehookKnownDevices() below restores the device vtables.
            g_input_callback(g_known_input_iface);
        }
    }
    for (void* iface : release_later) ComRelease(iface);
    erin::RehookKnownDevices();
    return true;
}

void UninstallInputHooks() {
    std::vector<void*> pending;
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_input_callback = nullptr;
        g_input_accepting = false;
        pending.swap(g_pending_input_ifaces);
        // g_known_input_iface is deliberately kept for a later install.
    }
    for (void* iface : pending) ComRelease(iface);
    erin::Uninstall();
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
    const HRESULT hr = g_system_create(hinst, version, riid, out, outer);
    if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (g_input_callback) {
            // Keep the first interface so a later install can re-hook CreateDevice.
            if (g_known_input_iface == nullptr) {
                ComAddRef(*out);
                g_known_input_iface = *out;
            }
            g_input_callback(*out);
        } else if (g_input_accepting) {
            // The loader thread registers the callback right after the environment
            // gate. Interfaces that appear before it are queued and their
            // CreateDevice is hooked late; devices created before that late hook
            // are not covered. Hooking before the gate was rejected because it
            // would touch an EAC-protected host.
            ComAddRef(*out);
            g_pending_input_ifaces.push_back(*out);
        }
    }
    return hr;
}

} // namespace eldenring::loader_detail
