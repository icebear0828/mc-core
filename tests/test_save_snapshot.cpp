#include <gtest/gtest.h>

#include "mc/save_snapshot.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace mc;

namespace {

SaveSnapshot makeSnapshot() {
    SaveSnapshot s;
    s.save_id = 0x1122334455667788ull;
    s.blocks = {{Cell{1, 2, 3}, BlockId::Stone}, {Cell{-4, 5, -6}, BlockId::Torch}};
    s.inventory[0] = {ItemId::DiamondSword, 1};
    s.inventory[17] = {ItemId::BlockDirt, 64};
    s.inventory[35] = {ItemId::Coal, 7};
    s.selected_slot = 3;
    s.position = Vec3{12.5f, 64.f, -7.25f};
    s.health = 17.5f;
    s.hunger = 11.f;
    s.xp_level = 9;
    s.xp_progress = 0.375f;
    s.flags = kSnapshotCreative | kSnapshotFlying;
    s.world_seed = 0xDEADBEEFu;
    s.game_time = 123456u;
    return s;
}

void expectSameSnapshot(const SaveSnapshot& a, const SaveSnapshot& b) {
    EXPECT_EQ(a.save_id, b.save_id);
    ASSERT_EQ(a.blocks.size(), b.blocks.size());
    for (size_t i = 0; i < a.blocks.size(); ++i) {
        EXPECT_EQ(a.blocks[i].cell, b.blocks[i].cell) << "block " << i;
        EXPECT_EQ(a.blocks[i].block, b.blocks[i].block) << "block " << i;
    }
    ASSERT_EQ(a.inventory.size(), b.inventory.size());
    for (size_t i = 0; i < a.inventory.size(); ++i) {
        EXPECT_EQ(a.inventory[i].item, b.inventory[i].item) << "slot " << i;
        EXPECT_EQ(a.inventory[i].count, b.inventory[i].count) << "slot " << i;
    }
    EXPECT_EQ(a.selected_slot, b.selected_slot);
    EXPECT_EQ(a.position.x, b.position.x);
    EXPECT_EQ(a.position.y, b.position.y);
    EXPECT_EQ(a.position.z, b.position.z);
    EXPECT_EQ(a.health, b.health);
    EXPECT_EQ(a.hunger, b.hunger);
    EXPECT_EQ(a.xp_level, b.xp_level);
    EXPECT_EQ(a.xp_progress, b.xp_progress);
    EXPECT_EQ(a.flags, b.flags);
    EXPECT_EQ(a.world_seed, b.world_seed);
    EXPECT_EQ(a.game_time, b.game_time);
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

uint32_t readU32(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

} // namespace

TEST(SaveSnapshot, RoundTripsEveryField) {
    const SaveSnapshot original = makeSnapshot();
    const std::vector<uint8_t> bytes = original.serialize();

    std::string error = "untouched";
    const std::optional<SaveSnapshot> loaded = SaveSnapshot::deserialize(bytes, &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    EXPECT_TRUE(error.empty());
    expectSameSnapshot(original, *loaded);
}

TEST(SaveSnapshot, SerializationIsDeterministic) {
    const std::vector<uint8_t> first = makeSnapshot().serialize();
    const std::vector<uint8_t> second = makeSnapshot().serialize();
    EXPECT_EQ(first, second);
}

TEST(SaveSnapshot, WritesTheDocumentedLittleEndianHeader) {
    const std::vector<uint8_t> bytes = makeSnapshot().serialize();
    ASSERT_GE(bytes.size(), kSaveSnapshotHeaderSize);
    EXPECT_EQ(bytes[0], static_cast<uint8_t>('M'));
    EXPECT_EQ(bytes[1], static_cast<uint8_t>('C'));
    EXPECT_EQ(bytes[2], static_cast<uint8_t>('S'));
    EXPECT_EQ(bytes[3], static_cast<uint8_t>('V'));
    EXPECT_EQ(bytes[4], static_cast<uint8_t>(kSaveSnapshotVersion & 0xFF));
    EXPECT_EQ(bytes[5], static_cast<uint8_t>((kSaveSnapshotVersion >> 8) & 0xFF));
    EXPECT_EQ(bytes[6], 0);
    EXPECT_EQ(bytes[7], 0);
    // save_id follows the two 16-bit fields and is little-endian.
    EXPECT_EQ(readU32(bytes, 8), 0x55667788u);
    EXPECT_EQ(readU32(bytes, 12), 0x11223344u);
    // payload_size is everything after the header.
    EXPECT_EQ(readU32(bytes, 16), static_cast<uint32_t>(bytes.size() - kSaveSnapshotHeaderSize));
}

TEST(SaveSnapshot, RejectsBadMagic) {
    std::vector<uint8_t> bytes = makeSnapshot().serialize();
    bytes[0] = 'X';
    std::string error;
    EXPECT_FALSE(SaveSnapshot::deserialize(bytes, &error).has_value());
    EXPECT_TRUE(contains(error, "magic")) << error;
}

TEST(SaveSnapshot, RejectsUnknownVersion) {
    std::vector<uint8_t> bytes = makeSnapshot().serialize();
    bytes[4] = 0x63; // version 99
    bytes[5] = 0x00;
    std::string error;
    EXPECT_FALSE(SaveSnapshot::deserialize(bytes, &error).has_value());
    EXPECT_TRUE(contains(error, "version")) << error;
}

TEST(SaveSnapshot, RejectsTruncatedHeader) {
    std::vector<uint8_t> bytes = makeSnapshot().serialize();
    bytes.resize(10);
    std::string error;
    EXPECT_FALSE(SaveSnapshot::deserialize(bytes, &error).has_value());
    EXPECT_TRUE(contains(error, "truncated")) << error;
}

TEST(SaveSnapshot, RejectsTruncatedPayload) {
    const std::vector<uint8_t> full = makeSnapshot().serialize();
    ASSERT_GT(full.size(), kSaveSnapshotHeaderSize + 8);

    // Header only: the declared payload is missing entirely.
    const std::vector<uint8_t> header_only(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(kSaveSnapshotHeaderSize));
    std::string error;
    EXPECT_FALSE(SaveSnapshot::deserialize(header_only, &error).has_value());
    EXPECT_TRUE(contains(error, "truncated")) << error;

    // Cut inside the payload while the header still declares the original size.
    std::vector<uint8_t> cut = full;
    cut.resize(cut.size() - 5);
    error.clear();
    EXPECT_FALSE(SaveSnapshot::deserialize(cut, &error).has_value());
    EXPECT_TRUE(contains(error, "truncated")) << error;
}

TEST(SaveSnapshot, RejectsTrailingBytes) {
    std::vector<uint8_t> bytes = makeSnapshot().serialize();
    bytes.push_back(0xAB);
    std::string error;
    EXPECT_FALSE(SaveSnapshot::deserialize(bytes, &error).has_value());
    EXPECT_TRUE(contains(error, "trailing")) << error;
}

TEST(SaveSnapshot, RejectsCorruptedCrc) {
    std::vector<uint8_t> bytes = makeSnapshot().serialize();
    ASSERT_GT(bytes.size(), kSaveSnapshotHeaderSize + 2);
    bytes[kSaveSnapshotHeaderSize + 1] ^= 0xFF;
    std::string error;
    EXPECT_FALSE(SaveSnapshot::deserialize(bytes, &error).has_value());
    EXPECT_TRUE(contains(error, "crc")) << error;
}

TEST(SaveSnapshot, RejectsUnknownHeaderFlags) {
    std::vector<uint8_t> bytes = makeSnapshot().serialize();
    bytes[6] = 0x80; // header flags are reserved for version 1
    std::string error;
    EXPECT_FALSE(SaveSnapshot::deserialize(bytes, &error).has_value());
    EXPECT_TRUE(contains(error, "flags")) << error;
}

TEST(SaveSnapshot, EmptySnapshotRoundTrips) {
    const SaveSnapshot empty;
    const std::vector<uint8_t> bytes = empty.serialize();
    EXPECT_EQ(bytes.size(), kSaveSnapshotHeaderSize + kSaveSnapshotEmptyPayloadSize);

    std::string error;
    const std::optional<SaveSnapshot> loaded = SaveSnapshot::deserialize(bytes, &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    expectSameSnapshot(empty, *loaded);
    EXPECT_TRUE(loaded->blocks.empty());
    for (const ItemStack& stack : loaded->inventory) {
        EXPECT_TRUE(stack.empty());
    }
}

TEST(SaveSnapshot, LargeSnapshotRoundTrips) {
    SaveSnapshot original;
    original.save_id = 42;
    original.blocks.reserve(2000);
    for (int i = 0; i < 2000; ++i) {
        const BlockId block = (i % 2 == 0) ? BlockId::Stone : BlockId::Glass;
        original.blocks.push_back({Cell{i, -i, i * 2}, block});
    }
    for (size_t i = 0; i < original.inventory.size(); ++i) {
        original.inventory[i] = {ItemId::BlockDirt, static_cast<uint32_t>(i % 64) + 1};
    }
    original.selected_slot = 8;
    original.health = 20.f;
    original.hunger = 20.f;
    original.world_seed = 7u;
    original.game_time = 6000u;

    const std::vector<uint8_t> bytes = original.serialize();
    std::string error;
    const std::optional<SaveSnapshot> loaded = SaveSnapshot::deserialize(bytes, &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    expectSameSnapshot(original, *loaded);
}

TEST(SaveSnapshot, DeserializeWithoutAnErrorSinkStillReportsFailure) {
    std::vector<uint8_t> bytes = makeSnapshot().serialize();
    bytes[0] = 'X';
    EXPECT_FALSE(SaveSnapshot::deserialize(bytes, nullptr).has_value());
    EXPECT_FALSE(SaveSnapshot::deserialize(std::span<const uint8_t>{}, nullptr).has_value());
}

TEST(SaveSlotStore, FlushRestoreContainsAndDrop) {
    SaveSlotStore store;
    EXPECT_EQ(store.size(), 0u);
    EXPECT_TRUE(store.ids().empty());
    EXPECT_FALSE(store.contains(7));
    EXPECT_FALSE(store.restore(7).has_value());

    SaveSnapshot first = makeSnapshot();
    first.save_id = 7; // flush rebinds the snapshot to the id it is stored under
    ASSERT_TRUE(store.flush(7, first));
    EXPECT_TRUE(store.contains(7));
    EXPECT_EQ(store.size(), 1u);

    const std::optional<SaveSnapshot> loaded = store.restore(7);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->save_id, 7u);
    expectSameSnapshot(first, *loaded);

    SaveSnapshot second = makeSnapshot();
    second.health = 3.f;
    ASSERT_TRUE(store.flush(7, second)); // same id replaces in place
    EXPECT_EQ(store.size(), 1u);
    ASSERT_TRUE(store.restore(7).has_value());
    EXPECT_EQ(store.restore(7)->health, 3.f);

    store.drop(7);
    EXPECT_FALSE(store.contains(7));
    EXPECT_EQ(store.size(), 0u);
    EXPECT_FALSE(store.restore(7).has_value());

    store.drop(7); // dropping an absent id is a no-op
    EXPECT_EQ(store.size(), 0u);
}

TEST(SaveSlotStore, RejectsTheInvalidSaveId) {
    SaveSlotStore store;
    EXPECT_FALSE(store.flush(kInvalidSaveId, makeSnapshot()));
    EXPECT_EQ(store.size(), 0u);
    EXPECT_FALSE(store.contains(kInvalidSaveId));
}

TEST(SaveSlotStore, EvictsTheLeastRecentlyUsedSlot) {
    SaveSlotStore store;
    for (SaveId id = 1; id <= SaveSlotStore::kMaxSlots; ++id) {
        SaveSnapshot s;
        s.save_id = id;
        ASSERT_TRUE(store.flush(id, s));
    }
    EXPECT_EQ(store.size(), SaveSlotStore::kMaxSlots);

    const SaveId overflow = SaveSlotStore::kMaxSlots + 1;
    ASSERT_TRUE(store.flush(overflow, SaveSnapshot{}));
    EXPECT_EQ(store.size(), SaveSlotStore::kMaxSlots);
    EXPECT_FALSE(store.contains(1)); // oldest flush lost
    EXPECT_TRUE(store.contains(2));
    EXPECT_TRUE(store.contains(overflow));
}

TEST(SaveSlotStore, RestoreRefreshesTheSlotSoItSurvivesEviction) {
    SaveSlotStore store;
    for (SaveId id = 1; id <= SaveSlotStore::kMaxSlots; ++id) {
        SaveSnapshot s;
        s.save_id = id;
        ASSERT_TRUE(store.flush(id, s));
    }
    const SaveSlotStore& read_only = store;
    ASSERT_TRUE(read_only.restore(1).has_value()); // touch the oldest slot

    const SaveId overflow = SaveSlotStore::kMaxSlots + 1;
    ASSERT_TRUE(store.flush(overflow, SaveSnapshot{}));
    EXPECT_TRUE(store.contains(1));  // kept: restored most recently
    EXPECT_FALSE(store.contains(2)); // now the least recently used
    EXPECT_EQ(store.size(), SaveSlotStore::kMaxSlots);
}

TEST(SaveSlotStore, IdsAreOrderedFromOldestToNewestUse) {
    SaveSlotStore store;
    ASSERT_TRUE(store.flush(10, SaveSnapshot{}));
    ASSERT_TRUE(store.flush(20, SaveSnapshot{}));
    ASSERT_TRUE(store.flush(30, SaveSnapshot{}));
    EXPECT_EQ(store.ids(), (std::vector<SaveId>{10, 20, 30}));

    ASSERT_TRUE(store.restore(10).has_value());
    EXPECT_EQ(store.ids(), (std::vector<SaveId>{20, 30, 10}));

    store.tick();
    store.tick();
    ASSERT_TRUE(store.flush(20, SaveSnapshot{}));
    EXPECT_EQ(store.ids(), (std::vector<SaveId>{30, 10, 20}));
}

TEST(SaveSlotStore, SurvivesManyOverwritesWithoutGrowing) {
    SaveSlotStore store;
    for (int i = 0; i < 100; ++i) {
        SaveSnapshot s;
        s.game_time = static_cast<uint32_t>(i);
        ASSERT_TRUE(store.flush(5, s));
    }
    EXPECT_EQ(store.size(), 1u);
    ASSERT_TRUE(store.restore(5).has_value());
    EXPECT_EQ(store.restore(5)->game_time, 99u);
}
