#pragma once

// Binding to the live Sekiro process. Everything here is platform independent: memory is accessed
// through IMemoryReader, so the logic is unit-tested against fake memory on any host.
//
// Offsets and signatures were verified against a running Sekiro (Steam, 2026-10-06) by memory
// inspection: positions are in metres (sprint ~5.6 u/s), Y is up, the basis is left-handed.

#include "sekiro_native.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
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
// The player's model object ([ChrIns+0x48]) holds the character's world transform at +0x30 as a row-major
// 3x4 [R | t]: R a rotation about Y, t the player's position (so t doubles as a structural check). Measured
// on the running game by correlating it with the run direction over 140 samples; the model faces -Z, so the
// heading is (-R02, -R22) in (x, z).
inline constexpr uintptr_t kChrModelInChrIns = 0x48;
inline constexpr uintptr_t kModelTransform = 0x30;
inline constexpr float kFacingTolerance = 0.02f;
inline constexpr float kMaxModelPositionDelta = 1.0f;
// Vitals: ChrIns+0x10b8 -> module container, container+0x1e8 -> SprjChrDataModule (RTTI vtable RVA below, used as
// the class check), int32 current hp at +0x130 and max hp at +0x138. Verified in the live game with damage and
// healing: 1120 -> 934 -> 748 ... -> 128, healing -> 1024; max stayed 1120.
inline constexpr uintptr_t kModuleContainerInChrIns = 0x10b8;
inline constexpr uintptr_t kChrDataModuleInContainer = 0x1e8;
inline constexpr uint32_t kChrDataModuleVtableRva = 0x2A8BE18;
inline constexpr uintptr_t kDataHp = 0x130;
inline constexpr uintptr_t kDataMaxHp = 0x138;
inline constexpr int32_t kMaxPlausibleMaxHp = 100000;
// Enemies: WorldChrMan+0xC8 -> WorldBlockChr (class-checked); +0x80 int32 slot count (144 seen), +0x88 pointer
// to the slot array, stride 0x38, EnemyIns* at +0 of each slot (null when empty). Verified in the live game.
// +0xE0 on a character is its static spawn position (it does not follow the character); the live one is
// +0x1050 like the player's, and all-zero there means the character is not loaded/active.
inline constexpr uintptr_t kWorldBlockInWorldChrMan = 0xC8;
inline constexpr uint32_t kWorldBlockVtableRva = 0x2A2EB10;
inline constexpr uintptr_t kBlockSlotCount = 0x80;
inline constexpr uintptr_t kBlockSlotArray = 0x88;
inline constexpr uintptr_t kSlotStride = 0x38;
inline constexpr int32_t kMaxSlots = 4096;
inline constexpr uint32_t kEnemyInsVtableRva = 0x2A27F28;
inline constexpr uintptr_t kEnemyCharId = 0x68;     // uint32, e.g. 10010000
inline constexpr uintptr_t kEnemyTeam = 0x70;       // uint32: 0/1 player side, 5 hostile, 9 neutral
inline constexpr uint32_t kTeamHostile = 5;
inline constexpr uintptr_t kEnemyDataModuleInContainer = 0x1f8; // enemies: +0x1f8 (the player's is +0x1e8)
inline constexpr uintptr_t kEnemyMaxHp = 0x160;                 // max hp at +0x160 (read 1120 on the player and 2101 on an enemy)
inline constexpr uintptr_t kModuleScanLimit = 0x400;
inline constexpr uintptr_t kOwnerInDataModule = 0x8;            // the character a data module belongs to
inline constexpr uintptr_t kCharacterScanLimit = 0x3000;        // how far into a character object its module pointer is looked for            // how far into a module container to look for the data module
// Fall module: [container+0x240] is SprjPlayerFallModule; int32 at +0x40 is -1 on the ground and >= 0 airborne.
inline constexpr uintptr_t kFallModuleInContainer = 0x240;
inline constexpr uint32_t kFallModuleVtableRva = 0x2A821F0;
inline constexpr uintptr_t kFallState = 0x40;
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

// A character's SprjChrDataModule inside its module container. The player's hangs at +0x1e8 and some enemies'
// at +0x1f8, but most soldiers have something else at those offsets, so the container is searched for a
// pointer to an object with the data module's vtable; `hint_offset` (a previous result) is tried first.
struct ContainerModuleRef {
    uintptr_t module{0};
    uintptr_t offset{0}; // within the container
};
std::optional<ContainerModuleRef> findChrDataModule(const IMemoryReader& reader, uintptr_t image_base, uintptr_t container,
                                               uintptr_t hint_offset = 0);

// How a character reaches its data module. Most enemy types point at it straight from the character object
// (`first` = offset in the character, `second` = kDirect); a few go through a sub-object (`first` = offset in the
// character, `second` = offset in that object). A module is only accepted when its own `+0x8` names the character
// as its owner, so another character's health is never read (or written) by mistake.
struct DataModuleRef {
    static constexpr uintptr_t kDirect = ~uintptr_t{0};
    uintptr_t module{0};
    uintptr_t first{0};
    uintptr_t second{kDirect};
    [[nodiscard]] bool isDirect() const { return second == kDirect; }
};
std::optional<DataModuleRef> findOwnedDataModule(const IMemoryReader& reader, uintptr_t image_base, size_t image_size,
                                                 uintptr_t owner, const DataModuleRef* hint = nullptr);

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
    // The player's real health points. Optional: never fails a sample.
    bool vitals_valid{false};
    float hp{0.0f};
    float max_hp{0.0f};
    // The game's own ground flag. Optional: unknown reads as "grounded" so nothing glides by accident.
    bool grounded_valid{false};
    bool grounded{true};
};

// One loaded character of the world block, as read from the live game this frame.
struct LiveEnemy {
    uintptr_t handle{0}; // the host's address: identity within this frame only, never an EntityId
    uint32_t slot{0};
    uint32_t char_id{0};
    uint32_t team{0};
    bool hostile{false};
    native::FVector3 position{};
    bool hp_valid{false};
    float hp{0.0f};
    float max_hp{0.0f};
    bool dead{false}; // only ever true on a valid reading of 0 hp
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

    // Active enemies of the current world block (up to `max_entries`). Every pointer is class-checked by its
    // RTTI vtable before it is read; garbage never produces an entry. Returns the number appended.
    size_t enumerateEnemies(std::vector<LiveEnemy>& out, size_t max_entries = 512) const;

    [[nodiscard]] uint32_t worldChrManGlobalRva() const { return wcm_global_rva_; }
    [[nodiscard]] uintptr_t imageBase() const { return base_; }
    [[nodiscard]] size_t imageSize() const { return size_; }
    [[nodiscard]] const std::vector<uint32_t>& cameraCandidateRvas() const { return camera_global_rvas_; }

private:
    bool readPointer(uintptr_t address, uintptr_t& out) const;
    bool readCamera(uintptr_t object, LiveSample& out) const;
    void readFacing(uintptr_t player, LiveSample& out) const;
    // Where the data module was last found per enemy address (a search hint; always re-validated by vtable).
    mutable std::map<uintptr_t, DataModuleRef> module_hints_;
    // Characters that had no data module: do not repeat the full search every frame, retry every so often.
    mutable std::map<uintptr_t, unsigned> module_retry_at_;
    mutable unsigned enumerate_calls_{0};
    void readVitals(uintptr_t player, LiveSample& out) const;
    void readGrounded(uintptr_t player, LiveSample& out) const;

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
// The game's camera object only changes every other frame (measured: 232 of 360 frames repeat the previous
// camera) while the player moves every frame. On a frame where the camera did not change, keep it at the
// same offset from the player as when it last did. Never extrapolates for more than a couple of frames, so a
// camera that really stopped following is not dragged along.
class CameraStabilizer {
public:
    static constexpr int kMaxCarriedFrames = 2;
    static constexpr float kMaxJump = 5.0f; // metres of player motion between frames above which we reset

    void apply(LiveSample& sample);
    void reset() { have_previous_ = false; carried_ = 0; }

private:
    bool have_previous_{false};
    int carried_{0};
    native::FVector3 last_player_{};
    native::FVector3 last_camera_{};
    native::FVector3 offset_{};
};

class LiveMirror {
public:
    void update(const LiveSample& sample, float dt, native::ChrIns& player, native::ChrCam& camera);
    void reset() { have_previous_ = false; }

private:
    bool have_previous_{false};
    native::FVector3 previous_{};
};

} // namespace sekiro::live
