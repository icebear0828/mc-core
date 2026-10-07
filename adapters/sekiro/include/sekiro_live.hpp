#pragma once

// Binding to the live Sekiro process. Everything here is platform independent: memory is accessed
// through IMemoryReader, so the logic is unit-tested against fake memory on any host.
//
// Offsets and signatures were verified against a running Sekiro (Steam, 2026-10-06) by memory
// inspection: positions are in metres (sprint ~5.6 u/s), Y is up, the basis is left-handed.

#include "sekiro_native.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace sekiro::live {

namespace layout {

// `mov rsi,[rip+rel32]` before `movaps xmm11,[rax]`; unique in the decrypted image.
inline constexpr std::string_view kWorldChrManAob = "48 8B 35 ?? ?? ?? ?? 44 0F 28 18";
// Getter returning the float at [obj+0x160]; the global it loads is the camera object.
// Several globals can match, so candidates are validated structurally at sample time.
inline constexpr std::string_view kCameraAob = "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 09 F3 0F 10 80 60 01 00 00 C3";

inline constexpr uint32_t kRipDispOffset = 3; // rel32 follows the 3-byte opcode
inline constexpr uint32_t kRipInstrLength = 7;

inline constexpr uintptr_t kPlayerInsInWorldChrMan = 0x88;
inline constexpr uintptr_t kChrPosition = 0x1050;     // float x,y,z (metres)
inline constexpr uintptr_t kChrPositionCopy = 0x1060; // second copy, used as a consistency check
// The player's model object ([ChrIns+0x48]) holds the body heading as (0, qy, 0, qw) at +0x2c. Measured on
// the running game: walking straight ahead gave exactly the camera's forward vector as (-qw, -qy) in (x, z),
// the block is stable while only the camera turns, and it follows the character when it turns.
inline constexpr uintptr_t kChrModelInChrIns = 0x48;
inline constexpr uintptr_t kFacingBlock = 0x2c; // float x, y, z, w
inline constexpr float kFacingTolerance = 0.02f;
inline constexpr uintptr_t kCameraMatrix = 0xea0;     // row-major 4x4: right, up, forward, position(w=1)
inline constexpr uintptr_t kCameraFov = 0x160;        // float, returned by the camera getter the signature matches

inline constexpr float kMaxCoordinate = 1.0e5f;
inline constexpr float kMaxPositionCopyDelta = 1.0f;
inline constexpr float kBasisTolerance = 0.02f;
inline constexpr float kMaxPlausibleSpeed = 200.0f; // m/s; faster is a teleport, not motion
inline constexpr float kMinFov = 0.2f;              // radians; outside [kMinFov, kMaxFov] the value is not trusted
inline constexpr float kMaxFov = 2.6f;

} // namespace layout

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> mask; // 1 = byte must match, 0 = wildcard

    // "48 8B ?? 05": hex bytes or ?/?? wildcards. Any invalid token yields an empty pattern.
    static Pattern parse(std::string_view text);
};

class IMemoryReader {
public:
    virtual ~IMemoryReader() = default;
    // Must fail (return false) instead of faulting on unreadable memory.
    virtual bool read(uintptr_t address, void* out, size_t size) const = 0;
};

// RVAs of every match, or nullopt when part of the image could not be read (e.g. not mapped yet).
std::optional<std::vector<uint32_t>> findPattern(const IMemoryReader& reader, uintptr_t base, size_t size,
                                                 const Pattern& pattern, size_t chunk = 1u << 20);

struct LiveSample {
    native::FVector3 player_pos;
    native::FVector3 cam_pos;
    native::FVector3 cam_right;
    native::FVector3 cam_up;
    native::FVector3 cam_forward;
    float cam_fov_y{0.0f}; // radians; 0 = unknown (caller picks a default)
    // Body heading of the player as a horizontal unit vector (native X/Z). Optional: never fails a sample.
    bool facing_valid{false};
    float facing_x{0.0f};
    float facing_z{1.0f};
};

enum class BindStatus { Searching, Ambiguous, Bound };
enum class SampleStatus { NotBound, NotInWorld, Invalid, Ok };

class LiveBinder {
public:
    LiveBinder(const IMemoryReader& reader, uintptr_t image_base, size_t image_size, size_t chunk = 1u << 20);

    // Scans the image. The Steam wrapper decrypts code lazily, so this keeps returning Searching
    // until the signatures appear. Once Bound it does no further work.
    BindStatus scan();
    [[nodiscard]] BindStatus status() const { return status_; }

    // Validated read of the live state. Never guesses: anything implausible is reported, not used.
    SampleStatus sample(LiveSample& out) const;

    [[nodiscard]] uint32_t worldChrManGlobalRva() const { return wcm_global_rva_; }
    [[nodiscard]] uintptr_t imageBase() const { return base_; }
    [[nodiscard]] size_t imageSize() const { return size_; }
    [[nodiscard]] const std::vector<uint32_t>& cameraCandidateRvas() const { return camera_global_rvas_; }

private:
    bool readPointer(uintptr_t address, uintptr_t& out) const;
    bool readCamera(uintptr_t object, LiveSample& out) const;
    void readFacing(uintptr_t player, LiveSample& out) const;

    const IMemoryReader& reader_;
    uintptr_t base_;
    size_t size_;
    size_t chunk_;
    BindStatus status_{BindStatus::Searching};
    uint32_t wcm_global_rva_{0};
    std::vector<uint32_t> camera_global_rvas_;
    mutable size_t camera_hint_{0}; // last candidate that validated (render thread only)
};

// Copies live readings into the adapter-side mirror structures. Only touches position, velocity and
// camera; model visibility flags stay owned by the adapter.
class LiveMirror {
public:
    void update(const LiveSample& sample, float dt, native::ChrIns& player, native::ChrCam& camera);
    void reset() { have_previous_ = false; }

private:
    bool have_previous_{false};
    native::FVector3 previous_{};
};

} // namespace sekiro::live
