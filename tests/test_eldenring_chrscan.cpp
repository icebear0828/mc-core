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
    EXPECT_EQ(showDrawnBit(0x000000A6u), 0x000000A7u);
    EXPECT_EQ(showDrawnBit(hideDrawnBit(0x000100A1u)), 0x000100A1u); // hide then show gives the original
    EXPECT_EQ(showDrawnBit(0x000000A7u), 0x000000A7u);
}

TEST(EldenRingChrScan, TheDrawnBitIsTheLowestBitOnly) {
    EXPECT_EQ(hideDrawnBit(0x000000A7u), 0x000000A6u);
    EXPECT_EQ(hideDrawnBit(0x000100A1u), 0x000100A0u);
    EXPECT_EQ(hideDrawnBit(0x000000A6u), 0x000000A6u); // already hidden: nothing to write
    EXPECT_TRUE(needsHiding(0x000000A7u));
    EXPECT_FALSE(needsHiding(0x000000A6u));
}

TEST(EldenRingChrScan, SummonsAreTheTeamTheWolvesWereSeenWith) {
    EXPECT_EQ(summon::kTeam, 47);
    EXPECT_EQ(summon::kDispFlags1, 0x20u); // same word as the player's parts (eldenring_model.hpp layout::kDispFlags1)
}
