#include "mc/save_snapshot.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mc {
namespace {

constexpr std::array<uint32_t, 256> makeCrcTable() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit) {
            c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[static_cast<size_t>(i)] = c;
    }
    return table;
}

inline constexpr std::array<uint32_t, 256> kCrcTable = makeCrcTable();

// CRC-32 (IEEE 802.3): reflected, polynomial 0xEDB88320, initial and final xor 0xFFFFFFFF.
uint32_t crc32(std::span<const uint8_t> bytes) {
    uint32_t c = 0xFFFFFFFFu;
    for (const uint8_t byte : bytes) {
        c = kCrcTable[(c ^ byte) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

// Explicit little-endian writer. Nothing is memcpy'd, so the blob never depends on the host's
// struct padding or endianness.
class Writer {
public:
    void u8(uint8_t value) { bytes_.push_back(value); }
    void u16(uint16_t value) {
        u8(static_cast<uint8_t>(value & 0xFFu));
        u8(static_cast<uint8_t>((value >> 8) & 0xFFu));
    }
    void u32(uint32_t value) {
        u8(static_cast<uint8_t>(value & 0xFFu));
        u8(static_cast<uint8_t>((value >> 8) & 0xFFu));
        u8(static_cast<uint8_t>((value >> 16) & 0xFFu));
        u8(static_cast<uint8_t>((value >> 24) & 0xFFu));
    }
    void u64(uint64_t value) {
        u32(static_cast<uint32_t>(value & 0xFFFFFFFFull));
        u32(static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFull));
    }
    void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
    // Floats keep their exact bit pattern, so a round trip is bit exact even for NaN.
    void f32(float value) { u32(std::bit_cast<uint32_t>(value)); }
    void append(std::span<const uint8_t> bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }

    [[nodiscard]] const std::vector<uint8_t>& bytes() const { return bytes_; }
    [[nodiscard]] std::vector<uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<uint8_t> bytes_;
};

// Bounds-checked little-endian reader. Every call reports whether it had enough bytes left.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    bool u8(uint8_t& out) {
        if (bytes_.size() - pos_ < 1) return false;
        out = bytes_[pos_++];
        return true;
    }
    bool u16(uint16_t& out) {
        uint8_t lo = 0;
        uint8_t hi = 0;
        if (!u8(lo) || !u8(hi)) return false;
        out = static_cast<uint16_t>(static_cast<uint16_t>(lo) | static_cast<uint16_t>(static_cast<uint16_t>(hi) << 8));
        return true;
    }
    bool u32(uint32_t& out) {
        uint16_t lo = 0;
        uint16_t hi = 0;
        if (!u16(lo) || !u16(hi)) return false;
        out = static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16);
        return true;
    }
    bool u64(uint64_t& out) {
        uint32_t lo = 0;
        uint32_t hi = 0;
        if (!u32(lo) || !u32(hi)) return false;
        out = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        return true;
    }
    bool i32(int32_t& out) {
        uint32_t raw = 0;
        if (!u32(raw)) return false;
        out = static_cast<int32_t>(raw);
        return true;
    }
    bool f32(float& out) {
        uint32_t raw = 0;
        if (!u32(raw)) return false;
        out = std::bit_cast<float>(raw);
        return true;
    }

    [[nodiscard]] bool done() const { return pos_ == bytes_.size(); }

private:
    std::span<const uint8_t> bytes_;
    size_t pos_{0};
};

bool isKnownBlockId(uint8_t raw) { return raw <= static_cast<uint8_t>(BlockId::Torch); }

} // namespace

std::vector<uint8_t> SaveSnapshot::serialize() const {
    Writer payload;
    payload.u32(static_cast<uint32_t>(blocks.size()));
    for (const BlockRecord& record : blocks) {
        payload.i32(record.cell.x);
        payload.i32(record.cell.y);
        payload.i32(record.cell.z);
        payload.u8(static_cast<uint8_t>(record.block));
    }
    for (const ItemStack& stack : inventory) {
        payload.u16(static_cast<uint16_t>(stack.item));
        payload.u32(stack.count);
    }
    payload.i32(static_cast<int32_t>(selected_slot));
    payload.f32(position.x);
    payload.f32(position.y);
    payload.f32(position.z);
    payload.f32(health);
    payload.f32(hunger);
    payload.i32(static_cast<int32_t>(xp_level));
    payload.f32(xp_progress);
    payload.u32(flags);
    payload.u32(world_seed);
    payload.u32(game_time);

    const std::vector<uint8_t>& body = payload.bytes();
    Writer out;
    out.u32(kSaveSnapshotMagic);
    out.u16(kSaveSnapshotVersion);
    out.u16(0); // reserved header flags
    out.u64(save_id);
    out.u32(static_cast<uint32_t>(body.size()));
    out.u32(crc32(body));
    out.append(body);
    return out.take();
}

std::optional<SaveSnapshot> SaveSnapshot::deserialize(std::span<const uint8_t> bytes, std::string* error) {
    if (error != nullptr) error->clear();
    const auto fail = [error](std::string reason) -> std::optional<SaveSnapshot> {
        if (error != nullptr) *error = std::move(reason);
        return std::nullopt;
    };

    if (bytes.size() < kSaveSnapshotHeaderSize) return fail("truncated header");
    Reader header(bytes.first(kSaveSnapshotHeaderSize));
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t header_flags = 0;
    uint64_t save_id = 0;
    uint32_t payload_size = 0;
    uint32_t expected_crc = 0;
    header.u32(magic);
    header.u16(version);
    header.u16(header_flags);
    header.u64(save_id);
    header.u32(payload_size);
    header.u32(expected_crc);

    if (magic != kSaveSnapshotMagic) return fail("bad magic");
    if (version != kSaveSnapshotVersion) return fail("unsupported version " + std::to_string(version));
    if (header_flags != 0) return fail("unsupported header flags " + std::to_string(header_flags));

    const std::span<const uint8_t> payload = bytes.subspan(kSaveSnapshotHeaderSize);
    if (payload.size() < payload_size) return fail("truncated payload");
    if (payload.size() > payload_size) return fail("trailing bytes after payload");
    if (crc32(payload) != expected_crc) return fail("crc mismatch");

    Reader reader(payload);
    uint32_t block_count = 0;
    if (!reader.u32(block_count)) return fail("truncated payload");
    // Each record is a fixed size, so an absurd count is caught before reserving anything.
    if (static_cast<uint64_t>(block_count) * kSaveSnapshotBlockRecordSize > payload.size()) {
        return fail("truncated payload");
    }

    SaveSnapshot snapshot;
    snapshot.save_id = save_id;
    snapshot.blocks.reserve(block_count);
    for (uint32_t i = 0; i < block_count; ++i) {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        uint8_t block = 0;
        if (!reader.i32(x) || !reader.i32(y) || !reader.i32(z) || !reader.u8(block)) return fail("truncated payload");
        if (!isKnownBlockId(block)) return fail("unknown block id " + std::to_string(block));
        snapshot.blocks.push_back({Cell{x, y, z}, static_cast<BlockId>(block)});
    }
    for (ItemStack& stack : snapshot.inventory) {
        uint16_t item = 0;
        uint32_t count = 0;
        if (!reader.u16(item) || !reader.u32(count)) return fail("truncated payload");
        stack = {static_cast<ItemId>(item), count};
    }
    int32_t selected_slot = 0;
    if (!reader.i32(selected_slot)) return fail("truncated payload");
    snapshot.selected_slot = static_cast<int>(selected_slot);
    if (!reader.f32(snapshot.position.x) || !reader.f32(snapshot.position.y) || !reader.f32(snapshot.position.z)) {
        return fail("truncated payload");
    }
    if (!reader.f32(snapshot.health) || !reader.f32(snapshot.hunger)) return fail("truncated payload");
    int32_t xp_level = 0;
    if (!reader.i32(xp_level)) return fail("truncated payload");
    snapshot.xp_level = static_cast<int>(xp_level);
    if (!reader.f32(snapshot.xp_progress)) return fail("truncated payload");
    if (!reader.u32(snapshot.flags)) return fail("truncated payload");
    if (!reader.u32(snapshot.world_seed)) return fail("truncated payload");
    if (!reader.u32(snapshot.game_time)) return fail("truncated payload");
    if (!reader.done()) return fail("trailing bytes in payload");
    return snapshot;
}

bool SaveSlotStore::flush(SaveId id, SaveSnapshot snapshot) {
    if (id == kInvalidSaveId) return false;
    snapshot.save_id = id;
    const uint64_t now = ++clock_;

    if (Slot* existing = find(id)) {
        existing->snapshot = std::move(snapshot);
        existing->last_used = now;
        return true;
    }

    size_t index = kMaxSlots;
    for (size_t i = 0; i < kMaxSlots; ++i) {
        if (!slots_[i].used()) {
            index = i;
            break;
        }
    }
    if (index == kMaxSlots) index = oldest();

    slots_[index].id = id;
    slots_[index].snapshot = std::move(snapshot);
    slots_[index].last_used = now;
    return true;
}

std::optional<SaveSnapshot> SaveSlotStore::restore(SaveId id) const {
    const Slot* slot = find(id);
    if (slot == nullptr) return std::nullopt;
    slot->last_used = ++clock_; // the LRU clock is mutable: a restore keeps the slot alive
    return slot->snapshot;
}

bool SaveSlotStore::contains(SaveId id) const { return find(id) != nullptr; }

void SaveSlotStore::drop(SaveId id) {
    if (Slot* slot = find(id)) *slot = Slot{};
}

size_t SaveSlotStore::size() const {
    size_t count = 0;
    for (const Slot& slot : slots_) {
        if (slot.used()) ++count;
    }
    return count;
}

std::vector<SaveId> SaveSlotStore::ids() const {
    std::vector<std::pair<uint64_t, SaveId>> used;
    used.reserve(kMaxSlots);
    for (const Slot& slot : slots_) {
        if (slot.used()) used.emplace_back(slot.last_used, slot.id);
    }
    std::sort(used.begin(), used.end());
    std::vector<SaveId> out;
    out.reserve(used.size());
    for (const std::pair<uint64_t, SaveId>& entry : used) out.push_back(entry.second);
    return out;
}

void SaveSlotStore::tick() { ++clock_; }

SaveSlotStore::Slot* SaveSlotStore::find(SaveId id) {
    if (id == kInvalidSaveId) return nullptr;
    for (Slot& slot : slots_) {
        if (slot.used() && slot.id == id) return &slot;
    }
    return nullptr;
}

const SaveSlotStore::Slot* SaveSlotStore::find(SaveId id) const {
    if (id == kInvalidSaveId) return nullptr;
    for (const Slot& slot : slots_) {
        if (slot.used() && slot.id == id) return &slot;
    }
    return nullptr;
}

size_t SaveSlotStore::oldest() const {
    size_t best = 0;
    for (size_t i = 1; i < kMaxSlots; ++i) {
        if (slots_[i].last_used < slots_[best].last_used) best = i;
    }
    return best;
}

} // namespace mc
