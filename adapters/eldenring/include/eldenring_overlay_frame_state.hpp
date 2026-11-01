#pragma once

// Pure, headless-testable decisions used by the Elden Ring D3D12 overlay.
//
// This header deliberately includes neither d3d12.h nor ImGui so that mc_tests can
// compile and exercise the overlay's state machine, barrier plan and descriptor-slot
// arithmetic on every platform. The Windows-only renderer units implement the same
// contract; the tests in tests/test_eldenring_overlay_state.cpp pin it.

#include <cstdint>
#include <filesystem>
#include <string>

namespace erov {
namespace detail {

// Lifecycle of the D3D12 hook overlay.
//   Idle       - hooks installed, no DIRECT queue learned yet (or just resized)
//   QueueKnown - ExecuteCommandLists gave us the queue; Init may run
//   Ready      - device resources are live and frames are rendered
//   Resizing   - ResizeBuffers tore the resources down; the next Present rebuilds
//   Failed     - Init failed too often or the frame raised; the overlay is disabled
enum class HookPhase : std::uint8_t { Idle, QueueKnown, Ready, Failed, Resizing };

struct HookStateMachine {
    static constexpr int kMaxInitAttempts = 20;

    HookPhase phase{HookPhase::Idle};
    int init_attempts{0};

    // Returns true only the first time the queue is learned (Idle -> QueueKnown).
    bool on_queue_learned() {
        if (phase != HookPhase::Idle) return false;
        phase = HookPhase::QueueKnown;
        return true;
    }

    bool can_init() const { return phase != HookPhase::Failed && init_attempts < kMaxInitAttempts; }

    void on_init_ok() {
        if (phase == HookPhase::Failed) return;
        phase = HookPhase::Ready;
    }

    void on_init_failed() {
        if (phase == HookPhase::Failed) return;
        ++init_attempts;
        if (init_attempts >= kMaxInitAttempts) phase = HookPhase::Failed;
    }

    void on_resize() {
        if (phase == HookPhase::Ready) phase = HookPhase::Resizing;
    }

    void on_frame_rendered() {
        if (phase == HookPhase::Resizing) phase = HookPhase::Ready;
    }

    // A SEH fault while recording a frame disables the overlay immediately.
    void on_exception() { phase = HookPhase::Failed; }

    bool failed() const { return phase == HookPhase::Failed; }
};

// The back-buffer transitions a frame needs. The render target transition is always
// required; the screenshot copy is optional; the frame always returns to PRESENT.
struct FrameBarrierPlan {
    bool transition_to_rt{true};
    bool copy_screenshot{false};
    bool transition_to_present{true};
};

constexpr FrameBarrierPlan plan_frame_barriers(bool screenshot_requested) {
    return FrameBarrierPlan{true, screenshot_requested, true};
}

// Byte offset of a shader-visible descriptor slot from the heap base.
// The CPU and GPU handles use the same arithmetic; they only differ in the value width.
constexpr std::uint64_t srv_handle(std::uint64_t base, std::uint32_t increment, std::uint32_t slot) {
    return base + static_cast<std::uint64_t>(increment) * static_cast<std::uint64_t>(slot);
}

// F4 passes a bare filename; WIC writes relative to the process working directory,
// which is not the game folder. Relative paths are resolved next to the game exe.
inline std::string resolve_screenshot_path(const std::string& path, const std::string& exe_dir) {
    if (path.empty() || exe_dir.empty()) return path;
    const std::filesystem::path p(path);
    if (!p.is_relative()) return path;
    return (std::filesystem::path(exe_dir) / p).string();
}

} // namespace detail
} // namespace erov
