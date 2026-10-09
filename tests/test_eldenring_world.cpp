#include <gtest/gtest.h>

#include "eldenring_sigscan.hpp"
#include "eldenring_model.hpp"
#include "eldenring_world.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace eldenring::live;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr uintptr_t kWorld = 0x7ff500000000ull;
constexpr uintptr_t kSetPtrs = kWorld + layout::kChrSetPointerArrayInWorldChrMan;

class FakeMemory : public IMemoryReader {
public:
    std::map<uintptr_t, std::vector<uint8_t>> regions;
    mutable int reads_of_player_pos{0};
    // Hook used by the torn-read test: invoked on every read; may mutate the memory.
    std::function<void(uintptr_t)> on_read;

    bool read(uintptr_t address, void* out, size_t size) const override {
        if (on_read) const_cast<FakeMemory*>(this)->on_read(address);
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

// ---- team matrix -----------------------------------------------------------------------------------------

void addTeamMatrix(FakeMemory& m) {
    m.region(kBase + layout::kTeamMatrixRva, layout::kTeamCount * layout::kTeamCount * 8);
    const uintptr_t kinds[4] = {layout::kRelHostileRva, layout::kRelFriendRva, layout::kRelNeutralRva, layout::kRelAllRva};
    const uint32_t vtables[4] = {layout::kRelHostileVtableRva, layout::kRelFriendVtableRva, layout::kRelNeutralVtableRva,
                                 layout::kRelAllVtableRva};
    for (int i = 0; i < 4; ++i) {
        m.region(kBase + kinds[i], 8);
        m.put<uint64_t>(kBase + kinds[i], kBase + vtables[i]);
    }
    // Default everything NEUTRAL, then the measured rows for player team 1.
    for (uint32_t a = 0; a < layout::kTeamCount; ++a)
        for (uint32_t b = 0; b < layout::kTeamCount; ++b)
            m.put<uint64_t>(kBase + layout::kTeamMatrixRva + (a * layout::kTeamCount + b) * 8, kBase + layout::kRelNeutralRva);
    auto set = [&](uint32_t a, uint32_t b, uintptr_t kind) {
        m.put<uint64_t>(kBase + layout::kTeamMatrixRva + (a * layout::kTeamCount + b) * 8, kBase + kind);
    };
    for (uint32_t t : {6u, 7u, 27u, 48u, 51u}) {
        set(1, t, layout::kRelHostileRva);
        set(t, 1, layout::kRelHostileRva);
    }
    set(1, 1, layout::kRelFriendRva);
    set(1, 26, layout::kRelFriendRva);
    set(26, 1, layout::kRelFriendRva);
}

} // namespace

TEST(EldenRingTeam, ReadsMeasuredRelations) {
    FakeMemory m;
    addTeamMatrix(m);
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 6), TeamRelation::Hostile);
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 48), TeamRelation::Hostile);
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 26), TeamRelation::Friend);
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 1), TeamRelation::Friend);
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 0), TeamRelation::Neutral);
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 30), TeamRelation::Neutral);
}

TEST(EldenRingTeam, AllSingletonIsRecognised) {
    FakeMemory m;
    addTeamMatrix(m);
    m.put<uint64_t>(kBase + layout::kTeamMatrixRva + (1 * layout::kTeamCount + 9) * 8, kBase + layout::kRelAllRva);
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 9), TeamRelation::All);
}

TEST(EldenRingTeam, OutOfRangeTeamIsUnknown) {
    FakeMemory m;
    addTeamMatrix(m);
    EXPECT_EQ(readTeamRelation(m, kBase, 1, layout::kTeamCount), TeamRelation::Unknown);
    EXPECT_EQ(readTeamRelation(m, kBase, 255, 1), TeamRelation::Unknown);
}

TEST(EldenRingTeam, UnreadableMatrixIsUnknown) {
    FakeMemory m;
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 6), TeamRelation::Unknown);
}

TEST(EldenRingTeam, PointerToForeignObjectIsUnknown) {
    FakeMemory m;
    addTeamMatrix(m);
    m.put<uint64_t>(kBase + layout::kTeamMatrixRva + (1 * layout::kTeamCount + 6) * 8, 0x7ff600000000ull);
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 6), TeamRelation::Unknown);
}

TEST(EldenRingTeam, SingletonWithWrongVtableIsNotTrusted) {
    FakeMemory m;
    addTeamMatrix(m);
    m.put<uint64_t>(kBase + layout::kRelHostileRva, kBase + 0x1234); // build changed: not the hostile object
    EXPECT_EQ(readTeamRelation(m, kBase, 1, 6), TeamRelation::Unknown);
}

// ---- enumeration -----------------------------------------------------------------------------------------

namespace {

constexpr uintptr_t kPlayer = 0x7ff500100000ull;
constexpr uintptr_t kSet = 0x7ff500900000ull;
constexpr uintptr_t kEntries = 0x7ff500910000ull;

struct Spawned {
    uintptr_t chr;
};

// Lays out one character: ChrIns fields, module container, data module and physics module.
void spawnChr(FakeMemory& m, uintptr_t chr, uint64_t vtable_rva, int32_t npc_id, uint8_t team, int32_t hp, int32_t max_hp,
              float x, float y, float z) {
    const uintptr_t container = chr + 0x1000;
    const uintptr_t data = chr + 0x2000;
    const uintptr_t phys = chr + 0x3000;
    m.region(chr, 0x4000);
    m.put<uint64_t>(chr, kBase + vtable_rva);
    m.put<int32_t>(chr + layout::kNpcIdInChrIns, npc_id);
    m.put<uint8_t>(chr + layout::kTeamTypeInChrIns, team);
    m.put<uint64_t>(chr + layout::kModuleContainerInChrIns, container);
    m.put<uint64_t>(container + layout::kChrDataModuleSlot * 8, data);
    m.put<uint64_t>(container + layout::kPhysicsModuleSlot * 8, phys);
    m.put<uint64_t>(data, kBase + layout::kChrDataModuleVtableRva);
    m.put<uint64_t>(data + layout::kOwnerInDataModule, chr);
    m.put<int32_t>(data + layout::kDataHp, hp);
    m.put<int32_t>(data + layout::kDataMaxHp, max_hp);
    m.put<uint64_t>(phys, kBase + layout::kPhysicsModuleVtableRva);
    m.put<uint64_t>(phys + layout::kOwnerInDataModule, chr);
    m.put<float>(phys + layout::kPhysicsPosition + 0, x);
    m.put<float>(phys + layout::kPhysicsPosition + 4, y);
    m.put<float>(phys + layout::kPhysicsPosition + 8, z);
}

void setEntry(FakeMemory& m, size_t index, uintptr_t chr, uint64_t handle) {
    m.put<uint64_t>(kEntries + index * 16, chr);
    m.put<uint64_t>(kEntries + index * 16 + 8, handle);
}

FakeMemory makeWorld() {
    FakeMemory m;
    m.region(kBase + layout::kWorldChrManGlobalRva, 8);
    m.region(kWorld, 0x20000);
    m.put<uint64_t>(kBase + layout::kWorldChrManGlobalRva, kWorld);
    m.put<uint64_t>(kWorld + layout::kPlayerInsInWorldChrMan, kPlayer);
    spawnChr(m, kPlayer, layout::kPlayerInsVtableRva, 0, 1, 500, 500, 10.f, 2.f, 20.f);
    addTeamMatrix(m);
    m.region(kSet, 0x100);
    m.region(kEntries, 16 * 64);
    m.put<uint64_t>(kSetPtrs + 5 * 8, kSet);
    m.put<uint32_t>(kSet + layout::kChrSetCapacity, 64);
    m.put<uint64_t>(kSet + layout::kChrSetEntries, kEntries);
    return m;
}

} // namespace

TEST(EldenRingEnumerate, ReturnsRelativePositionAndHostility) {
    FakeMemory m = makeWorld();
    const uintptr_t wolf = 0x7ff501000000ull;
    spawnChr(m, wolf, layout::kEnemyInsVtableRva, 3000, 51, 80, 120, 13.f, 2.f, 24.f);
    setEntry(m, 0, wolf, 0xAAAA);
    std::vector<EnemyInfo> out;
    ASSERT_TRUE(enumerateEnemies(m, kBase, out, 16));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].chr, wolf);
    EXPECT_EQ(out[0].handle, 0xAAAAu);
    EXPECT_EQ(out[0].npc_id, 3000);
    EXPECT_EQ(out[0].team, 51);
    EXPECT_EQ(out[0].hp, 80);
    EXPECT_EQ(out[0].max_hp, 120);
    EXPECT_FLOAT_EQ(out[0].rel_x, 3.f);
    EXPECT_FLOAT_EQ(out[0].rel_y, 0.f);
    EXPECT_FLOAT_EQ(out[0].rel_z, 4.f);
    EXPECT_TRUE(out[0].hostile);
}

TEST(EldenRingEnumerate, NonHostileTeamsAreListedButNotHostile) {
    FakeMemory m = makeWorld();
    const uintptr_t troll = 0x7ff501000000ull;
    const uintptr_t ally = 0x7ff502000000ull;
    spawnChr(m, troll, layout::kEnemyInsVtableRva, 4300, 30, 900, 900, 15.f, 2.f, 20.f);
    spawnChr(m, ally, layout::kPlayerInsVtableRva, 100, 26, 300, 300, 12.f, 2.f, 20.f);
    setEntry(m, 0, troll, 1);
    setEntry(m, 1, ally, 2);
    std::vector<EnemyInfo> out;
    ASSERT_TRUE(enumerateEnemies(m, kBase, out, 16));
    ASSERT_EQ(out.size(), 2u);
    EXPECT_FALSE(out[0].hostile); // neutral
    EXPECT_FALSE(out[1].hostile); // friend; a humanoid PlayerIns is accepted
}

TEST(EldenRingEnumerate, UnknownRelationIsNeverHostile) {
    FakeMemory m = makeWorld();
    const uintptr_t e = 0x7ff501000000ull;
    spawnChr(m, e, layout::kEnemyInsVtableRva, 3000, 77, 80, 120, 13.f, 2.f, 24.f);
    m.put<uint64_t>(kBase + layout::kTeamMatrixRva + (1 * layout::kTeamCount + 77) * 8, 0x1234); // foreign object
    setEntry(m, 0, e, 1);
    std::vector<EnemyInfo> out;
    ASSERT_TRUE(enumerateEnemies(m, kBase, out, 16));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_FALSE(out[0].hostile);
}

TEST(EldenRingEnumerate, SkipsMainPlayerDeadGraceAndForeignClasses) {
    FakeMemory m = makeWorld();
    const uintptr_t dead = 0x7ff501000000ull, grace = 0x7ff502000000ull, junk = 0x7ff503000000ull, live = 0x7ff504000000ull;
    spawnChr(m, dead, layout::kEnemyInsVtableRva, 3000, 6, 0, 120, 13.f, 2.f, 24.f);
    spawnChr(m, grace, layout::kEnemyInsVtableRva, layout::kGraceNpcId, 0, 1939, 1939, 11.f, 2.f, 20.f);
    spawnChr(m, junk, 0x1234, 3000, 6, 50, 50, 13.f, 2.f, 24.f); // wrong vtable
    spawnChr(m, live, layout::kEnemyInsVtableRva, 3000, 6, 10, 120, 16.f, 2.f, 20.f);
    setEntry(m, 0, kPlayer, 0);
    setEntry(m, 1, dead, 1);
    setEntry(m, 2, grace, 2);
    setEntry(m, 3, junk, 3);
    setEntry(m, 4, 0, 0);
    setEntry(m, 5, live, 5);
    std::vector<EnemyInfo> out;
    ASSERT_TRUE(enumerateEnemies(m, kBase, out, 16));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].chr, live);
}

TEST(EldenRingEnumerate, DeduplicatesAndHonoursMax) {
    FakeMemory m = makeWorld();
    for (int i = 0; i < 5; ++i) {
        const uintptr_t e = 0x7ff501000000ull + i * 0x10000ull;
        spawnChr(m, e, layout::kEnemyInsVtableRva, 3000, 6, 10, 10, 11.f + i, 2.f, 20.f);
        setEntry(m, i, e, i + 1);
    }
    // Same character reachable from a second set.
    m.region(0x7ff500a00000ull, 0x100);
    m.put<uint64_t>(kSetPtrs + 6 * 8, 0x7ff500a00000ull);
    m.put<uint32_t>(0x7ff500a00000ull + layout::kChrSetCapacity, 64);
    m.put<uint64_t>(0x7ff500a00000ull + layout::kChrSetEntries, kEntries);
    std::vector<EnemyInfo> out;
    ASSERT_TRUE(enumerateEnemies(m, kBase, out, 16));
    EXPECT_EQ(out.size(), 5u);
    ASSERT_TRUE(enumerateEnemies(m, kBase, out, 3));
    EXPECT_EQ(out.size(), 3u);
}

TEST(EldenRingEnumerate, RejectsImplausibleSetCapacity) {
    FakeMemory m = makeWorld();
    const uintptr_t e = 0x7ff501000000ull;
    spawnChr(m, e, layout::kEnemyInsVtableRva, 3000, 6, 10, 10, 11.f, 2.f, 20.f);
    setEntry(m, 0, e, 1);
    m.put<uint32_t>(kSet + layout::kChrSetCapacity, 0x7fffffff);
    std::vector<EnemyInfo> out;
    ASSERT_TRUE(enumerateEnemies(m, kBase, out, 16));
    EXPECT_TRUE(out.empty());
}

TEST(EldenRingEnumerate, SkipsNonFinitePosition) {
    FakeMemory m = makeWorld();
    const uintptr_t e = 0x7ff501000000ull;
    spawnChr(m, e, layout::kEnemyInsVtableRva, 3000, 6, 10, 10, std::nanf(""), 2.f, 20.f);
    setEntry(m, 0, e, 1);
    std::vector<EnemyInfo> out;
    ASSERT_TRUE(enumerateEnemies(m, kBase, out, 16));
    EXPECT_TRUE(out.empty());
}

TEST(EldenRingEnumerate, ReadsTheOrientationQuaternionAndRejectsNonUnitOnes) {
    FakeMemory m = makeWorld();
    const uintptr_t phys = kPlayer + 0x3000;
    const float q[4] = {0.f, 0.9530834f, 0.f, 0.3027079f};
    for (int i = 0; i < 4; ++i) m.put<float>(phys + layout::kPhysicsOrientation + 4 * i, q[i]);
    float out[4] = {};
    ASSERT_TRUE(detail::readPhysicsOrientation(m, kBase, kPlayer, out));
    EXPECT_FLOAT_EQ(out[1], 0.9530834f);
    m.put<float>(phys + layout::kPhysicsOrientation + 4, 3.f); // no longer unit length
    EXPECT_FALSE(detail::readPhysicsOrientation(m, kBase, kPlayer, out));
    EXPECT_FALSE(detail::readPhysicsOrientation(m, kBase, 0, out));
}

TEST(EldenRingEnumerate, FailsOutsideAWorld) {
    FakeMemory m = makeWorld();
    std::vector<EnemyInfo> out;
    m.put<uint64_t>(kWorld + layout::kPlayerInsInWorldChrMan, 0);
    EXPECT_FALSE(enumerateEnemies(m, kBase, out, 16));
    m.put<uint64_t>(kBase + layout::kWorldChrManGlobalRva, 0);
    EXPECT_FALSE(enumerateEnemies(m, kBase, out, 16));
}

TEST(EldenRingEnumerate, DiscardsTickWhenPlayerPositionJumpsMidRead) {
    FakeMemory m = makeWorld();
    const uintptr_t e = 0x7ff501000000ull;
    spawnChr(m, e, layout::kEnemyInsVtableRva, 3000, 6, 10, 10, 13.f, 2.f, 24.f);
    setEntry(m, 0, e, 1);
    // The floating origin re-bases while we read the enemy: the player's position moves by 32 m.
    const uintptr_t player_phys = kPlayer + 0x3000 + layout::kPhysicsPosition;
    bool jumped = false;
    m.on_read = [&](uintptr_t addr) {
        if (!jumped && addr == e + 0x3000 + layout::kPhysicsPosition) {
            jumped = true;
            m.put<float>(player_phys, 10.f - 32.f);
        }
    };
    std::vector<EnemyInfo> out;
    EXPECT_FALSE(enumerateEnemies(m, kBase, out, 16));
    EXPECT_TRUE(out.empty());
}

TEST(EldenRingEnumerate, SmallPlayerMovementWithinOneTickIsAccepted) {
    FakeMemory m = makeWorld();
    const uintptr_t e = 0x7ff501000000ull;
    spawnChr(m, e, layout::kEnemyInsVtableRva, 3000, 6, 10, 10, 13.f, 2.f, 24.f);
    setEntry(m, 0, e, 1);
    const uintptr_t player_phys = kPlayer + 0x3000 + layout::kPhysicsPosition;
    bool moved = false;
    m.on_read = [&](uintptr_t addr) {
        if (!moved && addr == e + 0x3000 + layout::kPhysicsPosition) {
            moved = true;
            m.put<float>(player_phys, 10.2f);
        }
    };
    std::vector<EnemyInfo> out;
    EXPECT_TRUE(enumerateEnemies(m, kBase, out, 16));
    EXPECT_EQ(out.size(), 1u);
}

// ---- signatures ------------------------------------------------------------------------------------------

TEST(EldenRingSig, ParsesHexAndWildcards) {
    const auto sig = Signature::parse("48 8B ?? 05");
    ASSERT_TRUE(sig.has_value());
    EXPECT_EQ(sig->size(), 4u);
}

TEST(EldenRingSig, RejectsMalformedPatterns) {
    EXPECT_FALSE(Signature::parse("").has_value());
    EXPECT_FALSE(Signature::parse("48 8").has_value());
    EXPECT_FALSE(Signature::parse("zz 10").has_value());
    EXPECT_FALSE(Signature::parse("48  8B").has_value());
}

TEST(EldenRingSig, UniqueMatchBindsAndReportsOffset) {
    const std::vector<uint8_t> image = {0, 1, 0x48, 0x8B, 0x11, 0x05, 9, 9};
    const auto r = scanUnique(image.data(), image.size(), *Signature::parse("48 8B ?? 05"));
    EXPECT_EQ(r.status, ScanStatus::Unique);
    EXPECT_EQ(r.offset, 2u);
}

TEST(EldenRingSig, NoMatchFailsClosed) {
    const std::vector<uint8_t> image = {0, 1, 2, 3};
    EXPECT_EQ(scanUnique(image.data(), image.size(), *Signature::parse("48 8B")).status, ScanStatus::NotFound);
}

TEST(EldenRingSig, TwoMatchesFailClosedEvenIfOverlapping) {
    const std::vector<uint8_t> image = {0xAA, 0xAA, 0xAA, 0x00};
    EXPECT_EQ(scanUnique(image.data(), image.size(), *Signature::parse("AA AA")).status, ScanStatus::Ambiguous);
    const std::vector<uint8_t> two = {1, 2, 9, 1, 2};
    EXPECT_EQ(scanUnique(two.data(), two.size(), *Signature::parse("01 02")).status, ScanStatus::Ambiguous);
}

TEST(EldenRingSig, PatternLongerThanImageIsNotFound) {
    const std::vector<uint8_t> image = {1, 2};
    EXPECT_EQ(scanUnique(image.data(), image.size(), *Signature::parse("01 02 03")).status, ScanStatus::NotFound);
}

TEST(EldenRingSig, ShippedSignaturesParseToTheDocumentedLengths) {
    EXPECT_EQ(Signature::parse(sigs::kRaycastWrapper)->size(), 36u);
    EXPECT_EQ(Signature::parse(sigs::kRaycastCore)->size(), 37u);
    EXPECT_EQ(Signature::parse(sigs::kCheckTeamHostile)->size(), 32u);
    EXPECT_EQ(Signature::parse(sigs::kCheckTeamFriend)->size(), 32u);
    EXPECT_EQ(Signature::parse(sigs::kGetEffectiveTeamType)->size(), 19u);
    EXPECT_EQ(Signature::parse(sigs::kClampHp)->size(), 32u);
}

TEST(EldenRingSig, HostileAndFriendSignaturesDifferOnlyInTheTail) {
    const auto h = Signature::parse(sigs::kCheckTeamHostile);
    const auto f = Signature::parse(sigs::kCheckTeamFriend);
    std::vector<uint8_t> image(64, 0);
    // Build an image holding only the hostile function: the friend signature must not match it.
    const std::string hs = sigs::kCheckTeamHostile;
    size_t pos = 0;
    for (size_t i = 0; i < h->size(); ++i, pos += 3) image[8 + i] = static_cast<uint8_t>(std::stoi(hs.substr(pos, 2), nullptr, 16));
    EXPECT_EQ(scanUnique(image.data(), image.size(), *h).status, ScanStatus::Unique);
    EXPECT_EQ(scanUnique(image.data(), image.size(), *f).status, ScanStatus::NotFound);
}

// ---- loader environment self-check -----------------------------------------------------------------------

TEST(EldenRingEnv, OfflineWithSteamAppIdIsAllowed) {
    EXPECT_EQ(checkEnvironment({"start_protected_game.exe", "dxgi.dll", "kernel32.dll"}, true), EnvVerdict::Ok);
}

TEST(EldenRingEnv, EacModuleRefusesToBind) {
    EXPECT_EQ(checkEnvironment({"eldenring.exe", "EasyAntiCheat_EOS.dll"}, true), EnvVerdict::EacLoaded);
    EXPECT_EQ(checkEnvironment({"easyanticheat.sys"}, true), EnvVerdict::EacLoaded);
}

TEST(EldenRingEnv, MissingSteamAppIdRefusesToBind) {
    EXPECT_EQ(checkEnvironment({"eldenring.exe"}, false), EnvVerdict::MissingSteamAppId);
}

TEST(EldenRingEnv, EacTakesPriorityOverMissingAppId) {
    EXPECT_EQ(checkEnvironment({"EasyAntiCheat_EOS.dll"}, false), EnvVerdict::EacLoaded);
}

// ---- native model parts -------------------------------------------------------------------------------------

TEST(EldenRingModel, CollectsTheDispFlagAddressesOfEveryAttachedPart) {
    FakeMemory m = makeWorld();
    const uintptr_t model = 0x7ff500700000ull, part_a = 0x7ff500710000ull, part_b = 0x7ff500720000ull;
    const uintptr_t disp_a = 0x7ff500730000ull, disp_b = 0x7ff500740000ull;
    m.region(model - 8, 0x400);
    m.region(kBase + layout::kAsmModelVtableRva, 8);
    for (uintptr_t r : {part_a, part_b, disp_a, disp_b}) m.region(r, 0x100);
    m.put<uint64_t>(model, kBase + layout::kAsmModelVtableRva);
    m.put<uint64_t>(kPlayer + layout::kAsmModelInPlayerIns, model);
    m.put<uint64_t>(model + layout::kAsmPartPointers + 2 * 8, part_a);
    m.put<uint64_t>(model + layout::kAsmPartPointers + 9 * 8, part_b);
    m.put<uint64_t>(part_a + layout::kPartDispEntity, disp_a);
    m.put<uint64_t>(part_b + layout::kPartDispEntity, disp_b);
    m.put<uint32_t>(disp_a + layout::kDispFlags1, 0x000100A1);
    m.put<uint32_t>(disp_b + layout::kDispFlags1, 0x000100A1);
    const auto addrs = collectDispFlagAddresses(m, kBase, kPlayer);
    ASSERT_EQ(addrs.size(), 2u);
    EXPECT_EQ(addrs[0], disp_a + layout::kDispFlags1);
    EXPECT_EQ(addrs[1], disp_b + layout::kDispFlags1);
}

TEST(EldenRingModel, RefusesAnObjectOfAnotherClassAndSkipsEmptySlots) {
    FakeMemory m = makeWorld();
    const uintptr_t model = 0x7ff500700000ull;
    m.region(model, 0x400);
    m.put<uint64_t>(model, kBase + 0x1234); // wrong vtable
    m.put<uint64_t>(kPlayer + layout::kAsmModelInPlayerIns, model);
    EXPECT_TRUE(collectDispFlagAddresses(m, kBase, kPlayer).empty());
    EXPECT_TRUE(collectDispFlagAddresses(m, kBase, 0).empty());
    m.put<uint64_t>(kPlayer + layout::kAsmModelInPlayerIns, 0);
    EXPECT_TRUE(collectDispFlagAddresses(m, kBase, kPlayer).empty());
}

TEST(EldenRingModel, HideAndRestoreOnlyTouchMaskedBits) {
    using namespace eldenring::live;
    EXPECT_EQ(hideBits(0x000100A1u, 0x1u), 0x000100A0u);
    EXPECT_EQ(hideBits(0x000100A1u, 0x10001u), 0x000000A0u);
    // the game flipped bit 5 while hidden: restore must keep that change and only bring back the masked bits
    EXPECT_EQ(restoreBits(0x00010080u, 0x000100A1u, 0x10001u), 0x00010081u);
    EXPECT_EQ(restoreBits(0x000100A0u, 0x000100A1u, 0x1u), 0x000100A1u);
}

TEST(EldenRingModel, IsFallingReadsPhysicsByte) {
    FakeMemory m = makeWorld();
    const uintptr_t phys = kPlayer + 0x3000;
    EXPECT_FALSE(detail::readIsFalling(m, kBase, kPlayer));
    m.put<uint8_t>(phys + layout::kIsFallingInPhysics, 1);
    EXPECT_TRUE(detail::readIsFalling(m, kBase, kPlayer));
    EXPECT_FALSE(detail::readIsFalling(m, kBase, 0)); // unreadable chr
}
