#pragma once

// Save-id keyed world snapshots. The host game stores a save id in each of its own save files; when
// the player saves, mc-core captures the Minecraft side (placed blocks, inventory, player state)
// under that id, and when the player loads an older host save it restores the matching snapshot, so
// the builds and the inventory rewind together with the host world (SkyCraft DESIGN.md section 11).
//
// A snapshot is a plain, engine-free capture. serialize() writes an explicit little-endian blob and
// deserialize() validates it; no struct is memcpy'd, so the layout does not depend on padding,
// alignment or endianness.
//
// Versioning contract: kSaveSnapshotVersion identifies the payload layout. Bump it whenever a field
// is added, removed, reordered or changes width; a reader only accepts its own version, so an older
// blob is rejected with an error instead of being misparsed. BlockId and ItemId values are part of
// that contract: they are append-only in mc-core, and a snapshot that carries a block id this build
// does not know is rejected rather than loaded as a different block.
//
// Header (24 bytes):
//   0  u32  magic        'M','C','S','V'
//   4  u16  version      kSaveSnapshotVersion
//   6  u16  flags        reserved, must be 0 for version 1
//   8  u64  save_id      the host save this snapshot belongs to
//  16  u32  payload_size bytes after the header
//  20  u32  crc32        CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) of the payload
//
// Payload:
//   u32 block_count, then per block: i32 x, i32 y, i32 z, u8 block id
//   36 x (u16 item id, u32 count)
//   i32 selected_slot
//   f32 position x, y, z
//   f32 health, f32 hunger, i32 xp_level, f32 xp_progress, u32 flags
//   u32 world_seed, u32 game_time
//
// Floats are written as their IEEE-754 bit pattern, so a round trip is bit exact (including NaN).

#include "mc/blocks.hpp"
#include "mc/inventory.hpp"
#include "mc/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mc {

// Identity of one host save slot. 0 is "no save": nothing is ever stored under it.
using SaveId = uint64_t;

inline constexpr SaveId kInvalidSaveId = 0;

inline constexpr uint32_t kSaveSnapshotMagic = 0x5653434Du; // bytes 'M','C','S','V' little-endian
inline constexpr uint16_t kSaveSnapshotVersion = 1;
inline constexpr size_t kSaveSnapshotHeaderSize = 24;
inline constexpr size_t kSaveSnapshotBlockRecordSize = 13;
inline constexpr size_t kSnapshotInventorySlots = 36;
inline constexpr size_t kSaveSnapshotEmptyPayloadSize = 4 + kSnapshotInventorySlots * 6 + 4 + 12 + 28;

static_assert(kSnapshotInventorySlots == Inventory::kSlots, "a snapshot stores the full survival inventory");

// Player state bits. The host decides what they mean; mc-core only carries them.
enum SaveSnapshotFlags : uint32_t {
    kSnapshotCreative = 1u << 0,
    kSnapshotFlying = 1u << 1,
    kSnapshotDead = 1u << 2,
};

struct BlockRecord {
    Cell cell{};
    BlockId block{BlockId::Air};

    bool operator==(const BlockRecord&) const = default;
};

struct SaveSnapshot {
    // The id this snapshot is bound to. SaveSlotStore::flush fills it in; deserialize() reads it
    // back from the header.
    SaveId save_id{kInvalidSaveId};

    // Every block the player placed or broke, in the order the host reported it. Only the sparse
    // diff against the host world is stored, not the whole world.
    std::vector<BlockRecord> blocks;

    std::array<ItemStack, kSnapshotInventorySlots> inventory{};
    int selected_slot{0};

    Vec3 position{};
    float health{20.f};
    float hunger{20.f};
    int xp_level{0};
    float xp_progress{0.f};
    uint32_t flags{0};

    // Enough to re-create the world the snapshot was taken in.
    uint32_t world_seed{0};
    uint32_t game_time{0};

    [[nodiscard]] std::vector<uint8_t> serialize() const;

    // Returns nullopt and, when `error` is not null, a short reason when the blob is malformed: a
    // bad magic, an unknown version or header flags, a truncated payload, trailing bytes or a CRC
    // mismatch. Never partially fills a snapshot.
    [[nodiscard]] static std::optional<SaveSnapshot> deserialize(std::span<const uint8_t> bytes,
                                                                 std::string* error = nullptr);
};

// A small in-memory LRU cache of snapshots keyed by SaveId. The adapter owns the disk I/O: it
// serializes a snapshot to persist it and calls flush() to keep it hot for a reload. Pure logic,
// no file access, not thread-safe.
class SaveSlotStore {
public:
    static constexpr size_t kMaxSlots = 8;

    // Stores `snapshot` under `id` and marks it the most recently used. Re-flushing an id replaces
    // it in place. When the store is full the least recently used slot is evicted. Returns false
    // only for kInvalidSaveId, which is never stored.
    bool flush(SaveId id, SaveSnapshot snapshot);

    // Returns the snapshot bound to `id`, or nullopt when it is absent. Refreshes the slot's
    // recency (the LRU clock is mutable) so a restore keeps it alive across an eviction.
    [[nodiscard]] std::optional<SaveSnapshot> restore(SaveId id) const;

    [[nodiscard]] bool contains(SaveId id) const;
    void drop(SaveId id);

    [[nodiscard]] size_t size() const;

    // The stored ids ordered from least to most recently used.
    [[nodiscard]] std::vector<SaveId> ids() const;

    // Advances the LRU clock by one without touching any slot. Use it to keep recency ordering
    // deterministic when several flushes would otherwise share a tick.
    void tick();

private:
    struct Slot {
        SaveId id{kInvalidSaveId};
        SaveSnapshot snapshot;
        mutable uint64_t last_used{0};
        [[nodiscard]] bool used() const { return id != kInvalidSaveId; }
    };

    [[nodiscard]] Slot* find(SaveId id);
    [[nodiscard]] const Slot* find(SaveId id) const;
    [[nodiscard]] size_t oldest() const;

    std::array<Slot, kMaxSlots> slots_{};
    mutable uint64_t clock_{0};
};

} // namespace mc
