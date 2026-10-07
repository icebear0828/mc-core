#include <gtest/gtest.h>

#include "eldenring_live.hpp"

#include <cstring>
#include <map>
#include <vector>

using namespace eldenring::live;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr uintptr_t kWorld = 0x7ff500000000ull;      // WorldChrManImp
constexpr uintptr_t kPlayer = 0x7ff500100000ull;     // PlayerIns
constexpr uintptr_t kContainer = 0x7ff500200000ull;  // [player+0x190]
constexpr uintptr_t kDataModule = 0x7ff500300000ull; // [container+0x0]
constexpr uintptr_t kOtherChr = 0x7ff500400000ull;

class FakeMemory : public IMemoryReader {
public:
    std::map<uintptr_t, std::vector<uint8_t>> regions;

    bool read(uintptr_t address, void* out, size_t size) const override {
        for (const auto& [start, bytes] : regions) {
            if (address >= start && address + size <= start + bytes.size()) {
                std::memcpy(out, bytes.data() + (address - start), size);
                return true;
            }
        }
        return false;
    }
    void region(uintptr_t start, size_t size) { regions[start].assign(size, 0); }
    template <typename T>
    void put(uintptr_t address, const T& value) {
        for (auto& [start, bytes] : regions) {
            if (address >= start && address + sizeof(T) <= start + bytes.size()) {
                std::memcpy(bytes.data() + (address - start), &value, sizeof(T));
                return;
            }
        }
        FAIL() << "address not in any region";
    }
};

// A world with a player whose module container holds a CSChrDataModule at slot 0 (hp 522 / max 553, i.e. the
// Crimson Amber Medallion worn, as measured live).
FakeMemory makeWorld(int32_t hp = 522, int32_t max_hp = 553) {
    FakeMemory m;
    m.region(kBase + layout::kWorldChrManGlobalRva, 8);
    m.region(kWorld, 0x20000);
    m.region(kPlayer, 0x400);
    m.region(kContainer, 0x200);
    m.region(kDataModule, 0x400);
    m.put<uint64_t>(kBase + layout::kWorldChrManGlobalRva, kWorld);
    m.put<uint64_t>(kWorld + layout::kPlayerInsInWorldChrMan, kPlayer);
    m.put<uint64_t>(kPlayer + layout::kModuleContainerInChrIns, kContainer);
    m.put<uint64_t>(kContainer + layout::kChrDataModuleSlot * sizeof(uint64_t), kDataModule);
    m.put<uint64_t>(kDataModule, kBase + layout::kChrDataModuleVtableRva);
    m.put<uint64_t>(kDataModule + layout::kOwnerInDataModule, kPlayer);
    m.put<int32_t>(kDataModule + layout::kDataHp, hp);
    m.put<int32_t>(kDataModule + layout::kDataMaxHp, max_hp);
    return m;
}

} // namespace

TEST(EldenRingVitals, ReadsHpAndEffectiveMax) {
    const FakeMemory m = makeWorld(522, 553);
    Vitals v;
    ASSERT_TRUE(readVitals(m, kBase, kPlayer, v));
    EXPECT_EQ(v.hp, 522);
    EXPECT_EQ(v.max_hp, 553);
}

TEST(EldenRingVitals, FullAndZeroHpAreValid) {
    FakeMemory m = makeWorld(553, 553);
    Vitals v;
    EXPECT_TRUE(readVitals(m, kBase, kPlayer, v));
    m.put<int32_t>(kDataModule + layout::kDataHp, 0);
    EXPECT_TRUE(readVitals(m, kBase, kPlayer, v));
    EXPECT_EQ(v.hp, 0);
}

TEST(EldenRingVitals, RejectsNullPlayer) {
    const FakeMemory m = makeWorld();
    Vitals v;
    EXPECT_FALSE(readVitals(m, kBase, 0, v));
}

TEST(EldenRingVitals, RejectsUnreadableContainerPointer) {
    FakeMemory m = makeWorld();
    m.put<uint64_t>(kPlayer + layout::kModuleContainerInChrIns, 0xdead0000ull);
    Vitals v;
    EXPECT_FALSE(readVitals(m, kBase, kPlayer, v));
}

TEST(EldenRingVitals, RejectsNullContainerOrModule) {
    FakeMemory m = makeWorld();
    Vitals v;
    m.put<uint64_t>(kContainer + layout::kChrDataModuleSlot * sizeof(uint64_t), 0);
    EXPECT_FALSE(readVitals(m, kBase, kPlayer, v));
    m.put<uint64_t>(kPlayer + layout::kModuleContainerInChrIns, 0);
    EXPECT_FALSE(readVitals(m, kBase, kPlayer, v));
}

TEST(EldenRingVitals, RejectsWrongVtable) {
    FakeMemory m = makeWorld();
    m.put<uint64_t>(kDataModule, kBase + 0x1234);
    Vitals v;
    EXPECT_FALSE(readVitals(m, kBase, kPlayer, v));
}

TEST(EldenRingVitals, RejectsModuleOwnedByAnotherCharacter) {
    FakeMemory m = makeWorld();
    m.put<uint64_t>(kDataModule + layout::kOwnerInDataModule, kOtherChr);
    Vitals v;
    EXPECT_FALSE(readVitals(m, kBase, kPlayer, v));
}

TEST(EldenRingVitals, RejectsImplausibleNumbers) {
    Vitals v;
    EXPECT_FALSE(readVitals(makeWorld(100, 0), kBase, kPlayer, v));               // no max
    EXPECT_FALSE(readVitals(makeWorld(100, -5), kBase, kPlayer, v));              // negative max
    EXPECT_FALSE(readVitals(makeWorld(-1, 553), kBase, kPlayer, v));              // negative hp
    EXPECT_FALSE(readVitals(makeWorld(600, 553), kBase, kPlayer, v));             // hp above max
    EXPECT_FALSE(readVitals(makeWorld(1, layout::kMaxPlausibleMaxHp + 1), kBase, kPlayer, v));
}

TEST(EldenRingVitals, LeavesOutputUntouchedOnFailure) {
    Vitals v{7, 9};
    EXPECT_FALSE(readVitals(makeWorld(-1, 553), kBase, kPlayer, v));
    EXPECT_EQ(v.hp, 7);
    EXPECT_EQ(v.max_hp, 9);
}

TEST(EldenRingVitals, ReadsThroughWorldChrManGlobal) {
    const FakeMemory m = makeWorld(300, 553);
    Vitals v;
    ASSERT_TRUE(readPlayerVitals(m, kBase, v));
    EXPECT_EQ(v.hp, 300);
    EXPECT_EQ(v.max_hp, 553);
}

TEST(EldenRingVitals, NotInWorldWhenGlobalOrPlayerIsNull) {
    FakeMemory m = makeWorld();
    Vitals v;
    m.put<uint64_t>(kWorld + layout::kPlayerInsInWorldChrMan, 0); // loading: no player yet
    EXPECT_FALSE(readPlayerVitals(m, kBase, v));
    m.put<uint64_t>(kBase + layout::kWorldChrManGlobalRva, 0);    // title screen
    EXPECT_FALSE(readPlayerVitals(m, kBase, v));
}
