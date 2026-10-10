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
