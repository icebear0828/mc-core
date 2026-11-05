// Host runtime seam (issue #177, P5): the IHostRuntime contract, driven by a
// fake runtime, and the REFramework skeleton, driven by a fake API vtable.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mc/contracts/host_runtime.hpp"
#include "runtime/reframework_runtime.hpp"

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
