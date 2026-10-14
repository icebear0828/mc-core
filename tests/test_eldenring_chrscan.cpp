#include <gtest/gtest.h>

#include "eldenring_chrscan.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace eldenring::live;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;

class Mem : public IMemoryReader {
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
    void put(uintptr_t address, const T& v) {
        for (auto& [start, bytes] : regions) {
            if (address >= start && address + sizeof(T) <= start + bytes.size()) {
                std::memcpy(bytes.data() + (address - start), &v, sizeof(T));
                return;
            }
        }
        FAIL() << "address not in any region";
    }
    void putString(uintptr_t address, const std::string& s) {
        for (size_t i = 0; i <= s.size(); ++i) put<char>(address + i, i < s.size() ? s[i] : '\0');
    }
};

void addRttiClass(Mem& m, uint32_t vtable_rva, uint32_t col_rva, uint32_t td_rva, const std::string& name) {
    m.region(kBase + vtable_rva - 8, 16);
    m.region(kBase + col_rva, 0x20);
    m.region(kBase + td_rva, 0x10 + name.size() + 1);
    m.put<uint64_t>(kBase + vtable_rva - 8, kBase + col_rva);
    m.put<uint32_t>(kBase + col_rva, 1);
    m.put<uint32_t>(kBase + col_rva + 0xC, td_rva);
    m.putString(kBase + td_rva + 0x10, name);
}

} // namespace

TEST(EldenRingChrScan, FindsPointersToObjectsWithRttiAndIgnoresTheRest) {
    Mem m;
    addRttiClass(m, 0x2B35980, 0x3000000, 0x2B35000, ".?AVCSChrAsmModelIns@CS@@");
    const uintptr_t chr = 0x7ff600001000ull, model = 0x7ff600005000ull, junk = 0x7ff600006000ull;
    m.region(chr, 0x800);
    m.region(model, 0x40);
    m.region(junk, 0x40);
    m.put<uint64_t>(model, kBase + 0x2B35980); // the model object's vtable
    m.put<uint64_t>(chr + 0x648, model);
    m.put<uint64_t>(junk, 0x1234);             // a vtable that is not RTTI
    m.put<uint64_t>(chr + 0x650, junk);
    m.put<uint64_t>(chr + 0x658, 0x42);        // not a pointer
    const auto hits = scanForClasses(m, kBase, chr, 0, 0x800);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].offset, 0x648u);
    EXPECT_EQ(hits[0].object, model);
    EXPECT_EQ(hits[0].cls, ".?AVCSChrAsmModelIns@CS@@");
}

TEST(EldenRingChrScan, StopsAtTheHitLimitAndHonoursTheRange) {
    Mem m;
    addRttiClass(m, 0x1000, 0x2000, 0x3000, ".?AVX@@");
    const uintptr_t chr = 0x7ff600001000ull, obj = 0x7ff600005000ull;
    m.region(chr, 0x100);
    m.region(obj, 0x10);
    m.put<uint64_t>(obj, kBase + 0x1000);
    for (uintptr_t off = 0; off < 0x100; off += 8) m.put<uint64_t>(chr + off, obj);
    EXPECT_EQ(scanForClasses(m, kBase, chr, 0, 0x100, 5).size(), 5u);
    EXPECT_EQ(scanForClasses(m, kBase, chr, 0x40, 0x60).size(), 4u);
    EXPECT_TRUE(scanForClasses(m, kBase, 0, 0, 0x100).empty());
}

TEST(EldenRingChrScan, FindsTheDisplayFlagsWordByItsLiveValue) {
    Mem m;
    const uintptr_t disp = 0x7ff600009000ull;
    m.region(disp, 0x40);
    m.put<uint32_t>(disp + 0x20, 0x000100A1u);
    m.put<uint32_t>(disp + 0x24, 0x00000001u);
    const auto at = findDwordOffsets(m, disp, 0, 0x40, 0x000100A1u);
    ASSERT_EQ(at.size(), 1u);
    EXPECT_EQ(at[0], 0x20u);
}

TEST(EldenRingChrScan, OnlyModelLikeClassesAreWorthLookingInto) {
    EXPECT_TRUE(looksLikeModelClass(".?AVCSChrModelIns@CS@@"));
    EXPECT_TRUE(looksLikeModelClass(".?AVCSModelDispEntity@CS@@"));
    EXPECT_FALSE(looksLikeModelClass(".?AVCSChrDataModule@CS@@"));
}

TEST(EldenRingChrScan, LineShowsOffsetAddressAndClassIndentedByDepth) {
    ScanHit h{0x648, 0x7ff600005000ull, ".?AVCSChrAsmModelIns@CS@@"};
    const std::string line = formatScanHit(h, 1);
    EXPECT_NE(line.find("+0x648"), std::string::npos);
    EXPECT_NE(line.find("CSChrAsmModelIns"), std::string::npos);
    EXPECT_NE(line.find("chrscan:   +0x648"), std::string::npos); // two spaces for depth 1
}

TEST(EldenRingChrScan, DumpsDwordsOfAnObjectAsOneHexLine) {
    Mem m;
    const uintptr_t obj = 0x7ff600009000ull;
    m.region(obj, 0x40);
    m.put<uint32_t>(obj + 0x20, 0x000100A1u);
    m.put<uint32_t>(obj + 0x24, 0x00000001u);
    const std::string line = dumpDwords(m, obj, 0x20, 0x28);
    EXPECT_EQ(line, "+0x20: 000100A1 00000001");
    EXPECT_EQ(dumpDwords(m, 0x7ff600010000ull, 0, 8), "+0x0: ??? ???"); // unreadable memory shows as ???
}

TEST(EldenRingChrScan, CollectsEveryDisplayEntityBehindEveryChrModel) {
    // wolf layout from the 2026-10-10 scan: chr+0x50 -> CSChrModelIns -> +0x18 and +0x188 CSModelDispEntity; some wolves also have a second
    // CSChrModelIns elsewhere. The offsets differ between entities, so the collector goes by class names.
    Mem m;
    addRttiClass(m, 0x2B00000, 0x3000000, 0x2B10000, ".?AVCSChrModelIns@CS@@");
    addRttiClass(m, 0x2B20000, 0x3001000, 0x2B30000, ".?AVCSModelDispEntity@CS@@");
    addRttiClass(m, 0x2B40000, 0x3002000, 0x2B50000, ".?AVEnemyCtrl@CS@@");
    const uintptr_t chr = 0x7ff600001000ull, model1 = 0x7ff600010000ull, model2 = 0x7ff600020000ull;
    const uintptr_t disp_a = 0x7ff600030000ull, disp_b = 0x7ff600040000ull, disp_c = 0x7ff600050000ull, ctrl = 0x7ff600060000ull;
    for (uintptr_t r : {chr, model1, model2, disp_a, disp_b, disp_c, ctrl}) m.region(r, r == chr ? 0xA00 : 0x400);
    m.put<uint64_t>(model1, kBase + 0x2B00000);
    m.put<uint64_t>(model2, kBase + 0x2B00000);
    m.put<uint64_t>(disp_a, kBase + 0x2B20000);
    m.put<uint64_t>(disp_b, kBase + 0x2B20000);
    m.put<uint64_t>(disp_c, kBase + 0x2B20000);
    m.put<uint64_t>(ctrl, kBase + 0x2B40000);
    m.put<uint64_t>(chr + 0x50, model1);
    m.put<uint64_t>(chr + 0x58, ctrl);        // not a model
    m.put<uint64_t>(chr + 0x640, model2);
    m.put<uint64_t>(model1 + 0x18, disp_a);
    m.put<uint64_t>(model1 + 0x188, disp_b);
    m.put<uint64_t>(model2 + 0x18, disp_c);
    m.put<uint64_t>(model1 + 0x120, model1);  // a pointer back to itself must not loop or double count
    const auto flags = collectChrDispFlagAddresses(m, kBase, chr);
    ASSERT_EQ(flags.size(), 3u);
    EXPECT_EQ(flags[0], disp_a + 0x20);
    EXPECT_EQ(flags[1], disp_b + 0x20);
    EXPECT_EQ(flags[2], disp_c + 0x20);
}

TEST(EldenRingChrScan, NothingToCollectForAnObjectWithoutAModel) {
    Mem m;
    const uintptr_t chr = 0x7ff600001000ull;
    m.region(chr, 0xA00);
    EXPECT_TRUE(collectChrDispFlagAddresses(m, kBase, chr).empty());
    EXPECT_TRUE(collectChrDispFlagAddresses(m, kBase, 0).empty());
}

TEST(EldenRingChrScan, ShowingAgainSetsOnlyTheBitWeCleared) {
    EXPECT_EQ(showDrawnBit(0x00000000u, 0x000000A7u), 0x000000A7u);
    EXPECT_EQ(showDrawnBit(hideDrawnBit(0x000100A1u), 0x000100A1u), 0x000100A1u); // hide then show gives the original
    EXPECT_EQ(showDrawnBit(0x000000A7u, 0x000000A7u), 0x000000A7u);
}

TEST(EldenRingChrScan, HideMaskClearsModelDrawnShadowAndGhostParticles) {
    // Wolf display flags 0xA7 contains drawn (1), model (2, 4), shadow (0x20), spirit particle/phantom (0x80)
    EXPECT_EQ(hideDrawnBit(0x000000A7u), 0x00000000u);
    EXPECT_EQ(hideDrawnBit(0x000100A1u), 0x00000000u);
    EXPECT_EQ(hideDrawnBit(0x00000000u), 0x00000000u); // already hidden: nothing to write
    EXPECT_TRUE(needsHiding(0x000000A7u));
    EXPECT_TRUE(needsHiding(0x000000A6u)); // 0xA6 still had ghost/particle bits; now needsHiding is true
    EXPECT_FALSE(needsHiding(0x00000000u));
}

TEST(EldenRingChrScan, SummonsAreTheTeamTheWolvesWereSeenWith) {
    EXPECT_EQ(summon::kTeam, 47);
    EXPECT_EQ(summon::kDispFlags1, 0x20u); // same word as the player's parts (eldenring_model.hpp layout::kDispFlags1)
    EXPECT_EQ(summon::kDispFlags2, 0x24u);
    EXPECT_EQ(summon::kHideMask1, 0x000100A7u);
}

TEST(EldenRingChrScan, IdentifiesSpiritSpEffectIds) {
    // 0x182 (386) is the phantom/ghost shader flag checked by the D3D12 pipeline at 0x1402C722C
    EXPECT_TRUE(isSpiritSpEffectId(summon::kPhantomShaderSpEffect));
    EXPECT_TRUE(isSpiritSpEffectId(0x182));
    // Non-visual / lifecycle SpEffects must NOT be blocked, otherwise FromSoftware's summon lifecycle
    // fails to finish initialization or corrupts memory allocations!
    EXPECT_FALSE(isSpiritSpEffectId(20297130)); // Lone Wolf modifiers
    EXPECT_FALSE(isSpiritSpEffectId(20297320)); // Kaiden Sellsword modifiers
    EXPECT_FALSE(isSpiritSpEffectId(231000));   // Request based
    EXPECT_FALSE(isSpiritSpEffectId(232000));
    EXPECT_FALSE(isSpiritSpEffectId(140700000));
    // Non-spirit SpEffects must not be suppressed
    EXPECT_FALSE(isSpiritSpEffectId(0x8F));    // creative::kLandingSkipSpEffect
    EXPECT_FALSE(isSpiritSpEffectId(0));
    EXPECT_FALSE(isSpiritSpEffectId(1000));
}

TEST(EldenRingChrScan, SuppressSfxSlotSetsSkipBit) {
    // Bit 7 of slot + 0x5a causes ChrConditionSfxSeSlot::Update (0x1404A2480) to jump to early exit (0x1404A2A4D)
    EXPECT_EQ(suppressSfxSlotFlags(0x0000u), 0x0080u);
    EXPECT_EQ(suppressSfxSlotFlags(0x0005u), 0x0085u);
    EXPECT_TRUE(needsSfxSuppression(0x0005u));
    EXPECT_FALSE(needsSfxSuppression(0x0085u));
    EXPECT_EQ(restoreSfxSlotFlags(0x0085u), 0x0005u);
}

TEST(EldenRingChrScan, CollectsChrConditionSfxSlotsFromEnemyIns) {
    Mem m;
    addRttiClass(m, 0x2A428B0, 0x32D6558, 0x3C844E8, ".?AVChrConditionSfxSeSlot@CS@@");
    const uintptr_t chr = 0x7ff600001000ull, slot = 0x7ff600009000ull;
    m.region(chr, 0xA00);
    m.region(slot, 0x100);
    m.put<uint64_t>(slot, kBase + 0x2A428B0); // vtable
    m.put<uint64_t>(chr + 0x318, slot);       // wolf ChrConditionSfxSeSlot pointer #1
    m.put<uint64_t>(chr + 0x908, slot);       // wolf ChrConditionSfxSeSlot pointer #2
    const auto slots = collectChrConditionSfxSlots(m, kBase, chr);
    ASSERT_EQ(slots.size(), 1u); // De-duplicated
    EXPECT_EQ(slots[0], slot);
}

TEST(EldenRingChrScan, DefaultMobYawOffsetIsZeroNotPi) {
    // Monsters and wolves have physics forward orientation aligned with model space;
    // adding an extra pi (180 deg) by default causes summons to face completely backwards!
    EXPECT_FLOAT_EQ(summon::kDefaultMobYawOffset, 0.0f);
}

TEST(EldenRingChrScan, SfxParticleVisibilityClearing) {
    // [sfx + 0x14] bit 6 (0x40) controls visible particle rendering (CSFD4SfxIns::SetVisible 0x140D8B840).
    // Clearing bit 6 (and ~0x40) renders the particle emitter invisible safely without corrupting node pools.
    uint32_t sfx_flags = 0x7Cu; // default flags from 0x140D8B365
    EXPECT_TRUE((sfx_flags & summon::kSfxVisibleBit) != 0);
    EXPECT_EQ(summon::hideSfxBitmask(sfx_flags), 0x3Cu);
    EXPECT_FALSE((summon::hideSfxBitmask(sfx_flags) & summon::kSfxVisibleBit) != 0);
}

TEST(EldenRingChrScan, ProbeActiveWithoutLogMatchesActiveCreativeHooks) {
    // Only the probes modifying player physics and survivability are active without log switch
    EXPECT_FALSE(isProbeActiveWithoutLog("sp effect query"));
    EXPECT_TRUE(isProbeActiveWithoutLog("fall height"));
    EXPECT_TRUE(isProbeActiveWithoutLog("kill wrapper"));
    EXPECT_TRUE(isProbeActiveWithoutLog("fall time exceeded"));
    EXPECT_FALSE(isProbeActiveWithoutLog("chr event dispatch"));
}

TEST(EldenRingChrScan, CollectsAllChrSfxSlotTypesFromEnemyIns) {
    Mem m;
    addRttiClass(m, 0x2A428B0, 0x32D6558, 0x3C844E8, ".?AVChrConditionSfxSeSlot@CS@@");
    addRttiClass(m, 0x2A43400, 0x32D6DD0, 0x3C845E8, ".?AVChrMultiSfxSlot@CS@@");
    addRttiClass(m, 0x2A43AA8, 0x32D70C8, 0x3C846E8, ".?AVChrSingleSfxSlot@CS@@");
    const uintptr_t chr = 0x7ff600001000ull;
    const uintptr_t cond_slot = 0x7ff600009000ull;
    const uintptr_t multi_slot = 0x7ff60000A000ull;
    const uintptr_t single_slot = 0x7ff60000B000ull;
    m.region(chr, 0xA00);
    m.region(cond_slot, 0x100);
    m.region(multi_slot, 0x100);
    m.region(single_slot, 0x100);
    m.put<uint64_t>(cond_slot, kBase + 0x2A428B0);
    m.put<uint64_t>(multi_slot, kBase + 0x2A43400);
    m.put<uint64_t>(single_slot, kBase + 0x2A43AA8);
    m.put<uint64_t>(chr + 0x318, cond_slot);
    m.put<uint64_t>(chr + 0x320, multi_slot);
    m.put<uint64_t>(chr + 0x328, single_slot);
    const auto slots = collectChrSfxSlots(m, kBase, chr);
    ASSERT_EQ(slots.size(), 3u);
    EXPECT_EQ(slots[0], cond_slot);
    EXPECT_EQ(slots[1], multi_slot);
    EXPECT_EQ(slots[2], single_slot);
}

TEST(EldenRingChrScan, ScanForClassesIgnoresNonCanonicalPointersAndOutOfBoundsVtables) {
    Mem m;
    addRttiClass(m, 0x2A428B0, 0x32D6558, 0x3C844E8, ".?AVChrConditionSfxSeSlot@CS@@");
    const uintptr_t chr = 0x7ff600001000ull;
    const uintptr_t valid_slot = 0x7ff600009000ull;
    const uintptr_t invalid_slot = 0x7ff60000A000ull;
    m.region(chr, 0xA00);
    m.region(valid_slot, 0x100);
    m.region(invalid_slot, 0x100);
    m.put<uint64_t>(valid_slot, kBase + 0x2A428B0);
    // invalid_slot has vtable outside image bounds
    m.put<uint64_t>(invalid_slot, 0x123456780000ull);

    m.put<uint64_t>(chr + 0x10, 0xFFFFFFFFFFFFFFFFull); // -1 (non-canonical kernel pointer)
    m.put<uint64_t>(chr + 0x18, 0x000000003F800000ull); // 1.0f float representation
    m.put<uint64_t>(chr + 0x20, 0x7ff600001001ull);     // unaligned pointer
    m.put<uint64_t>(chr + 0x28, invalid_slot);          // out of image bounds vtable
    m.put<uint64_t>(chr + 0x30, valid_slot);            // perfectly valid slot

    const auto hits = scanForClasses(m, kBase, chr, 0, 0xA00);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].object, valid_slot);
    EXPECT_EQ(hits[0].cls, ".?AVChrConditionSfxSeSlot@CS@@");
}

