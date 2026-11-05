// Host runtime seam (issue #177, P5): the IHostRuntime contract, driven by a
// fake runtime, the REFramework skeleton, driven by a fake API vtable, and the
// production dinput8 runtime, driven by probe bindings.
#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <vector>

#include "mc/contracts/host_runtime.hpp"
#include "runtime/reframework_runtime.hpp"

#ifdef _WIN32
#include "runtime/dinput8_runtime.hpp"
#endif

namespace {

using mc::host::HostConfig;
using mc::host::HostRuntimeInfo;
using mc::host::IHostRuntime;
using mc::host::InputCallbacks;
using mc::host::PresentCallbacks;

// Minimal IHostRuntime implementation that records the calls in order, so the
// contract's lifecycle and hook semantics can be asserted without a game.
class FakeRuntime final : public IHostRuntime {
public:
    bool initialize(const HostConfig& config) override {
        events.push_back("initialize");
        config_ = config;
        initialized_ = true;
        return true;
    }

    void shutdown() override {
        events.push_back("shutdown");
        initialized_ = false;
        present_installed_ = false;
        input_installed_ = false;
        frame_ = nullptr;
    }

    void setFrameCallback(std::function<void(float)> callback) override { frame_ = std::move(callback); }

    bool installPresentHooks(const PresentCallbacks& callbacks) override {
        events.push_back("install_present");
        present_installed_ = !callbacks.empty();
        present_ = callbacks;
        return true;
    }

    bool installInputHooks(const InputCallbacks& callbacks) override {
        events.push_back("install_input");
        input_installed_ = !callbacks.empty();
        input_ = callbacks;
        return true;
    }

    void log(std::string_view message) override { lines.emplace_back(message); }

    [[nodiscard]] const HostConfig& config() const override { return config_; }
    [[nodiscard]] const HostRuntimeInfo& info() const override { return info_; }

    void deliverFrame(float delta_seconds) {
        if (frame_) frame_(delta_seconds);
    }

    [[nodiscard]] bool initialized() const { return initialized_; }
    [[nodiscard]] bool present_installed() const { return present_installed_; }
    [[nodiscard]] bool input_installed() const { return input_installed_; }

    std::vector<std::string> events;
    std::vector<std::string> lines;

private:
    HostConfig config_;
    HostRuntimeInfo info_{"fake", "1", true, true};
    PresentCallbacks present_;
    InputCallbacks input_;
    std::function<void(float)> frame_;
    bool initialized_{false};
    bool present_installed_{false};
    bool input_installed_{false};
};

class FakeReframeworkApi final : public mc::host::IReframeworkHostApi {
public:
    bool initialize() override {
        events.push_back("api_initialize");
        return initialize_result;
    }

    void shutdown() override { events.push_back("api_shutdown"); }

    void onFrame(float delta_seconds) override {
        events.push_back("api_frame");
        last_delta = delta_seconds;
    }

    void log(const char* message) override { logs.emplace_back(message); }

    bool initialize_result{true};
    std::vector<std::string> events;
    std::vector<std::string> logs;
    float last_delta{0.f};
};

} // namespace

TEST(HostRuntimeContract, LifecycleOrderAndHookInstallation) {
    FakeRuntime runtime;
    HostConfig config;
    config.game_dir = "C:/game/";
    config.enable_input = true;

    ASSERT_TRUE(runtime.initialize(config));
    EXPECT_TRUE(runtime.initialized());
    EXPECT_EQ(runtime.config().game_dir, "C:/game/");
    EXPECT_TRUE(runtime.config().enable_input);
    EXPECT_EQ(runtime.info().name, "fake");

    float seen_delta = 0.f;
    runtime.setFrameCallback([&seen_delta](float delta_seconds) { seen_delta = delta_seconds; });
    runtime.deliverFrame(0.5f);
    EXPECT_FLOAT_EQ(seen_delta, 0.5f);

    PresentCallbacks present;
    present.on_present = [] {};
    ASSERT_TRUE(runtime.installPresentHooks(present));
    InputCallbacks input;
    input.on_direct_input_created = [] {};
    ASSERT_TRUE(runtime.installInputHooks(input));
    EXPECT_TRUE(runtime.present_installed());
    EXPECT_TRUE(runtime.input_installed());

    runtime.shutdown();
    EXPECT_FALSE(runtime.initialized());
    EXPECT_FALSE(runtime.present_installed());
    EXPECT_FALSE(runtime.input_installed());
    EXPECT_EQ(runtime.events,
              (std::vector<std::string>{"initialize", "install_present", "install_input", "shutdown"}));
}

TEST(HostRuntimeContract, EmptyCallbacksRemoveInstalledHooks) {
    FakeRuntime runtime;
    ASSERT_TRUE(runtime.initialize(HostConfig{}));

    PresentCallbacks present;
    present.on_present = [] {};
    ASSERT_TRUE(runtime.installPresentHooks(present));
    ASSERT_TRUE(runtime.present_installed());
    ASSERT_TRUE(runtime.installPresentHooks(PresentCallbacks{}));
    EXPECT_FALSE(runtime.present_installed());

    InputCallbacks input;
    input.on_direct_input_created = [] {};
    ASSERT_TRUE(runtime.installInputHooks(input));
    ASSERT_TRUE(runtime.input_installed());
    ASSERT_TRUE(runtime.installInputHooks(InputCallbacks{}));
    EXPECT_FALSE(runtime.input_installed());
}

TEST(ReframeworkRuntime, InitializeWithoutInjectedApiFailsAndLogs) {
    std::vector<std::string> lines;
    mc::host::ReframeworkRuntime runtime;
    runtime.setLogSink([&lines](std::string_view line) { lines.emplace_back(line); });

    EXPECT_FALSE(runtime.initialize(HostConfig{}));
    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines.front().find("IReframeworkHostApi"), std::string::npos) << lines.front();
}

// Contract: an empty callback set removes previously installed hooks, so it has
// to succeed even on a host that cannot install detours at all.
TEST(ReframeworkRuntime, EmptyCallbackSetsAreAccepted) {
    mc::host::ReframeworkRuntime runtime;
    EXPECT_TRUE(runtime.installPresentHooks(PresentCallbacks{}));
    EXPECT_TRUE(runtime.installInputHooks(InputCallbacks{}));
}

// MC_ENABLE_RE4 (default OFF) selects whether the Resident Evil 4 integration is
// compiled in. With the option off the runtime is a refusing stub, so the
// lifecycle tests only exist in the RE4 build; the default build asserts that
// the stub refuses to initialize.
#if MC_ENABLE_RE4

TEST(ReframeworkRuntime, FakeApiDrivesTheLifecycle) {
    FakeReframeworkApi api;
    mc::host::ReframeworkRuntime runtime(&api);
    HostConfig config;
    config.game_dir = "C:/re4/";
    config.enable_overlay = true;

    ASSERT_TRUE(runtime.initialize(config));
    ASSERT_EQ(api.events, (std::vector<std::string>{"api_initialize"}));
    EXPECT_EQ(runtime.config().game_dir, "C:/re4/");
    EXPECT_EQ(runtime.info().name, "reframework");

    float seen_delta = 0.f;
    runtime.setFrameCallback([&seen_delta](float delta_seconds) { seen_delta = delta_seconds; });
    runtime.onHostFrame(0.25f);
    ASSERT_EQ(api.events.size(), 2u);
    EXPECT_EQ(api.events[1], "api_frame");
    EXPECT_FLOAT_EQ(api.last_delta, 0.25f);
    EXPECT_FLOAT_EQ(seen_delta, 0.25f);

    runtime.shutdown();
    ASSERT_EQ(api.events.back(), "api_shutdown");
    EXPECT_FALSE(api.logs.empty());

    // Frames after shutdown must not reach the API or the callback.
    const size_t after_shutdown = api.events.size();
    runtime.onHostFrame(0.1f);
    EXPECT_EQ(api.events.size(), after_shutdown);
}

TEST(ReframeworkRuntime, ApiInitializationFailureAborts) {
    FakeReframeworkApi api;
    api.initialize_result = false;
    mc::host::ReframeworkRuntime runtime(&api);

    EXPECT_FALSE(runtime.initialize(HostConfig{}));
    ASSERT_EQ(api.events, (std::vector<std::string>{"api_initialize"}));

    // A failed runtime has nothing to shut down, and a second initialize retries.
    runtime.shutdown();
    EXPECT_EQ(api.events, (std::vector<std::string>{"api_initialize"}));
}

TEST(ReframeworkRuntime, PresentAndInputHooksAreUnsupported) {
    FakeReframeworkApi api;
    mc::host::ReframeworkRuntime runtime(&api);
    ASSERT_TRUE(runtime.initialize(HostConfig{}));

    PresentCallbacks present;
    present.on_present = [] {};
    EXPECT_FALSE(runtime.installPresentHooks(present));

    InputCallbacks input;
    input.on_direct_input_created = [] {};
    EXPECT_FALSE(runtime.installInputHooks(input));
}

#else

TEST(ReframeworkRuntime, InitializeRefusesWhenTheRe4IntegrationIsCompiledOut) {
    FakeReframeworkApi api;
    mc::host::ReframeworkRuntime runtime(&api);

    EXPECT_FALSE(mc::host::ReframeworkRuntime::compiled_with_re4());
    EXPECT_FALSE(runtime.initialize(HostConfig{}));
    EXPECT_TRUE(api.events.empty()) << "the stub must not drive the host API";
    ASSERT_FALSE(api.logs.empty());
    EXPECT_NE(api.logs.back().find("MC_ENABLE_RE4"), std::string::npos) << api.logs.back();
}

TEST(ReframeworkRuntime, StubRejectsDetourInstallationButAcceptsRemoval) {
    mc::host::ReframeworkRuntime runtime;

    PresentCallbacks present;
    present.on_present = [] {};
    EXPECT_FALSE(runtime.installPresentHooks(present));
    EXPECT_TRUE(runtime.installPresentHooks(PresentCallbacks{}));

    InputCallbacks input;
    input.on_direct_input_created = [] {};
    EXPECT_FALSE(runtime.installInputHooks(input));
    EXPECT_TRUE(runtime.installInputHooks(InputCallbacks{}));
}

#endif

#ifdef _WIN32

namespace {

std::atomic<int> g_loader_thread_runs{0};

DWORD WINAPI ProbeLoaderThread(LPVOID) {
    ++g_loader_thread_runs;
    return 0;
}

// The Bindings are plain function pointers, so the probe state lives at file
// scope; the fixture swaps it per test.
struct Dinput8Probe {
    int init_log_calls{0};
    int ensure_minhook_calls{0};
    int release_minhook_calls{0};
    bool ensure_minhook_result{true};
    bool install_present_result{true};
    bool install_input_result{true};
    int present_installs{0};
    int input_installs{0};
    bool last_present_has_callback{false};
    bool last_input_has_callback{false};
    std::vector<std::string> log_lines;
};

Dinput8Probe* g_probe = nullptr;

void ProbeInitLog() { ++g_probe->init_log_calls; }
bool ProbeEnsureMinHook() {
    ++g_probe->ensure_minhook_calls;
    return g_probe->ensure_minhook_result;
}
bool ProbeReleaseMinHook() {
    ++g_probe->release_minhook_calls;
    return true;
}
void ProbeLogLine(const char* line) { g_probe->log_lines.emplace_back(line); }
bool ProbeInstallPresent(const PresentCallbacks& callbacks) {
    ++g_probe->present_installs;
    g_probe->last_present_has_callback = static_cast<bool>(callbacks.on_present);
    return g_probe->install_present_result;
}
bool ProbeInstallInput(const InputCallbacks& callbacks) {
    ++g_probe->input_installs;
    g_probe->last_input_has_callback = static_cast<bool>(callbacks.on_direct_input_created);
    return g_probe->install_input_result;
}

mc::host::Dinput8Runtime::Bindings ProbeBindings() {
    mc::host::Dinput8Runtime::Bindings bindings;
    bindings.loader_thread = &ProbeLoaderThread;
    bindings.ensure_minhook = &ProbeEnsureMinHook;
    bindings.release_minhook = &ProbeReleaseMinHook;
    bindings.init_log = &ProbeInitLog;
    bindings.log_line = &ProbeLogLine;
    bindings.install_present = &ProbeInstallPresent;
    bindings.install_input = &ProbeInstallInput;
    return bindings;
}

class Dinput8RuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        probe = Dinput8Probe{};
        g_probe = &probe;
    }

    void TearDown() override { g_probe = nullptr; }

    Dinput8Probe probe;
};

} // namespace

TEST_F(Dinput8RuntimeTest, OnProcessAttachRequiresALoaderThread) {
    EXPECT_FALSE(mc::host::Dinput8Runtime::onProcessAttach(nullptr, mc::host::Dinput8Runtime::Bindings{}));
}

TEST_F(Dinput8RuntimeTest, OnProcessAttachStartsTheLoaderThread) {
    g_loader_thread_runs.store(0);
    ASSERT_TRUE(mc::host::Dinput8Runtime::onProcessAttach(nullptr, ProbeBindings()));

    for (int i = 0; i < 500 && g_loader_thread_runs.load() == 0; ++i) Sleep(10);
    EXPECT_EQ(g_loader_thread_runs.load(), 1);
}

TEST_F(Dinput8RuntimeTest, InitializeCopiesTheConfigAndBootstrapsTheLog) {
    mc::host::Dinput8Runtime runtime(ProbeBindings());
    HostConfig config;
    config.game_dir = "C:/game/";
    config.enable_overlay = true;

    ASSERT_TRUE(runtime.initialize(config));
    EXPECT_EQ(probe.init_log_calls, 1);
    EXPECT_EQ(runtime.config().game_dir, "C:/game/");
    EXPECT_TRUE(runtime.config().enable_overlay);
    EXPECT_FALSE(runtime.config().enable_input);
    EXPECT_EQ(runtime.info().name, "dinput8");

    ASSERT_FALSE(probe.log_lines.empty());
    EXPECT_NE(probe.log_lines.front().find("game_dir=C:/game/"), std::string::npos) << probe.log_lines.front();
    // HostConfig is the portable seam: no Elden Ring feature switches in the line.
    EXPECT_EQ(probe.log_lines.front().find("damage"), std::string::npos) << probe.log_lines.front();
    EXPECT_EQ(probe.log_lines.front().find("bullet"), std::string::npos) << probe.log_lines.front();

    // The hook engine is only started when the adapter actually installs hooks,
    // so initialize() cannot run it before the adapter's own environment checks.
    EXPECT_EQ(probe.ensure_minhook_calls, 0);
}

TEST_F(Dinput8RuntimeTest, InitializeIsIdempotent) {
    mc::host::Dinput8Runtime runtime(ProbeBindings());
    HostConfig config;
    config.game_dir = "C:/game/";

    ASSERT_TRUE(runtime.initialize(config));
    ASSERT_TRUE(runtime.initialize(config));
    EXPECT_EQ(probe.init_log_calls, 1);
}

TEST_F(Dinput8RuntimeTest, InstallHooksDelegateAndStartTheHookEngineOnce) {
    mc::host::Dinput8Runtime runtime(ProbeBindings());
    HostConfig config;
    config.game_dir = "C:/game/";
    config.enable_overlay = true;
    config.enable_input = true;
    ASSERT_TRUE(runtime.initialize(config));

    PresentCallbacks present;
    present.on_present = [] {};
    ASSERT_TRUE(runtime.installPresentHooks(present));
    EXPECT_EQ(probe.present_installs, 1);
    EXPECT_TRUE(probe.last_present_has_callback);
    EXPECT_EQ(probe.ensure_minhook_calls, 1);

    InputCallbacks input;
    input.on_direct_input_created = [] {};
    ASSERT_TRUE(runtime.installInputHooks(input));
    EXPECT_EQ(probe.input_installs, 1);
    EXPECT_TRUE(probe.last_input_has_callback);
    EXPECT_EQ(probe.ensure_minhook_calls, 1) << "the hook engine must be started once";

    runtime.shutdown();
    EXPECT_EQ(probe.release_minhook_calls, 1);
}

TEST_F(Dinput8RuntimeTest, EmptyCallbackSetsUninstallWithoutTheInstaller) {
    mc::host::Dinput8Runtime runtime(ProbeBindings());
    HostConfig config;
    config.enable_overlay = true;
    config.enable_input = true;
    ASSERT_TRUE(runtime.initialize(config));

    PresentCallbacks present;
    present.on_present = [] {};
    ASSERT_TRUE(runtime.installPresentHooks(present));
    EXPECT_TRUE(runtime.installPresentHooks(PresentCallbacks{}));
    EXPECT_EQ(probe.present_installs, 1) << "removal must not call the installer";

    InputCallbacks input;
    input.on_direct_input_created = [] {};
    ASSERT_TRUE(runtime.installInputHooks(input));
    EXPECT_TRUE(runtime.installInputHooks(InputCallbacks{}));
    EXPECT_EQ(probe.input_installs, 1);
}

TEST_F(Dinput8RuntimeTest, MissingInstallerIsReportedNotFaked) {
    mc::host::Dinput8Runtime::Bindings bindings = ProbeBindings();
    bindings.install_present = nullptr;
    bindings.install_input = nullptr;
    mc::host::Dinput8Runtime runtime(bindings);
    HostConfig config;
    config.enable_overlay = true;
    config.enable_input = true;
    ASSERT_TRUE(runtime.initialize(config));

    PresentCallbacks present;
    present.on_present = [] {};
    EXPECT_FALSE(runtime.installPresentHooks(present));

    InputCallbacks input;
    input.on_direct_input_created = [] {};
    EXPECT_FALSE(runtime.installInputHooks(input));

    ASSERT_FALSE(probe.log_lines.empty());
    EXPECT_NE(probe.log_lines.back().find("installer"), std::string::npos) << probe.log_lines.back();
}

TEST_F(Dinput8RuntimeTest, HooksDisabledByTheConfigAreRefused) {
    mc::host::Dinput8Runtime runtime(ProbeBindings());
    ASSERT_TRUE(runtime.initialize(HostConfig{})); // neither overlay nor input

    PresentCallbacks present;
    present.on_present = [] {};
    EXPECT_FALSE(runtime.installPresentHooks(present));

    InputCallbacks input;
    input.on_direct_input_created = [] {};
    EXPECT_FALSE(runtime.installInputHooks(input));

    EXPECT_EQ(probe.present_installs, 0);
    EXPECT_EQ(probe.input_installs, 0);
    EXPECT_EQ(probe.ensure_minhook_calls, 0);
}

TEST_F(Dinput8RuntimeTest, HookEngineFailureRejectsTheInstallation) {
    probe.ensure_minhook_result = false;
    mc::host::Dinput8Runtime runtime(ProbeBindings());
    HostConfig config;
    config.enable_input = true;
    ASSERT_TRUE(runtime.initialize(config));

    InputCallbacks input;
    input.on_direct_input_created = [] {};
    EXPECT_FALSE(runtime.installInputHooks(input));
    EXPECT_EQ(probe.input_installs, 0);
    EXPECT_EQ(probe.ensure_minhook_calls, 1);

    runtime.shutdown();
    EXPECT_EQ(probe.release_minhook_calls, 0) << "the runtime must not release an engine it never started";
}

TEST_F(Dinput8RuntimeTest, ShutdownIsIdempotent) {
    mc::host::Dinput8Runtime runtime(ProbeBindings());
    HostConfig config;
    config.enable_overlay = true;
    ASSERT_TRUE(runtime.initialize(config));

    PresentCallbacks present;
    present.on_present = [] {};
    ASSERT_TRUE(runtime.installPresentHooks(present));

    runtime.shutdown();
    runtime.shutdown();
    EXPECT_EQ(probe.release_minhook_calls, 1);
}

TEST_F(Dinput8RuntimeTest, LogFallsBackToTheDebuggerWithoutABoundSink) {
    // Before the adapter binds its log line, diagnostics can only go to the
    // debugger; the call must not crash or recurse into an unbound pointer.
    mc::host::Dinput8Runtime::Bindings bindings = ProbeBindings();
    bindings.log_line = nullptr;
    mc::host::Dinput8Runtime runtime(bindings);

    runtime.log("dinput8: early diagnostic");
    ASSERT_TRUE(runtime.initialize(HostConfig{}));
    EXPECT_TRUE(probe.log_lines.empty());
}

#endif // _WIN32
