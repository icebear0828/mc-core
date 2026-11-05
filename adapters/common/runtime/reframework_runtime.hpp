#pragma once

// REFramework host runtime skeleton (issue #177, P5) - Resident Evil 4.
//
// RE4 plugins are loaded by REFramework, which owns DllMain, the swapchain and
// the input pump. A plugin therefore cannot detour present or DirectInput itself;
// it registers callbacks with REFramework instead. This runtime models that with
// an injected API vtable, so the adapter and its tests build without any
// REFramework headers or SDK:
//
//   * no REFramework SDK header is included anywhere in this file
//   * the RE4 integration is compiled in only when MC_ENABLE_RE4=1 (default OFF)
//   * initialize() refuses to run without an injected IReframeworkHostApi
//
// The concrete vtable is supplied by the RE4 adapter's REFramework glue once the
// SDK is vendored; tests supply a fake.

#include <functional>
#include <string>
#include <string_view>

#include "mc/contracts/host_runtime.hpp"

#ifndef MC_ENABLE_RE4
#define MC_ENABLE_RE4 0
#endif

namespace mc::host {

// The slice of REFramework a plugin needs. Implemented by the RE4 adapter's
// glue and by tests, never by this runtime.
class IReframeworkHostApi {
public:
    virtual ~IReframeworkHostApi() = default;

    // Brings up the plugin context. False aborts the runtime.
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // Called once per rendered frame by the REFramework glue.
    virtual void onFrame(float delta_seconds) = 0;

    // REFramework's log sink (it prefixes plugin lines with the plugin name).
    virtual void log(const char* message) = 0;
};

class ReframeworkRuntime final : public IHostRuntime {
public:
    using LogSink = std::function<void(std::string_view)>;

    ReframeworkRuntime() = default;
    explicit ReframeworkRuntime(IReframeworkHostApi* api) noexcept : api_(api) {}

    ReframeworkRuntime(const ReframeworkRuntime&) = delete;
    ReframeworkRuntime& operator=(const ReframeworkRuntime&) = delete;

    // Injection point: the adapter sets the vtable before initialize().
    void setHostApi(IReframeworkHostApi* api) noexcept { api_ = api; }
    [[nodiscard]] IReframeworkHostApi* hostApi() const noexcept { return api_; }

    // Fallback sink for startup diagnostics, used while no API is injected.
    void setLogSink(LogSink sink) { sink_ = std::move(sink); }

    // Called by the REFramework glue once per rendered frame.
    void onHostFrame(float delta_seconds);

    [[nodiscard]] static constexpr bool compiled_with_re4() noexcept { return MC_ENABLE_RE4 != 0; }

    bool initialize(const HostConfig& config) override;
    void shutdown() override;
    void setFrameCallback(std::function<void(float)> callback) override;
    bool installPresentHooks(const PresentCallbacks& callbacks) override;
    bool installInputHooks(const InputCallbacks& callbacks) override;
    void log(std::string_view message) override;
    [[nodiscard]] const HostConfig& config() const override { return config_; }
    [[nodiscard]] const HostRuntimeInfo& info() const override { return info_; }

private:
    IReframeworkHostApi* api_{nullptr};
    LogSink sink_;
    HostConfig config_;
    HostRuntimeInfo info_{"reframework", "skeleton", false, false};
    std::function<void(float)> frame_;
    bool initialized_{false};
};

} // namespace mc::host
