#include <gtest/gtest.h>

#include "eldenring_handlescan.hpp"

#include <cstring>
#include <map>
#include <vector>

using namespace eldenring::live;
using namespace eldenring::handlescan;

namespace {
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
        for (auto& [start, bytes] : regions)
            if (address >= start && address + sizeof(T) <= start + bytes.size()) {
                std::memcpy(bytes.data() + (address - start), &v, sizeof(T));
                return;
            }
    }
};
constexpr uintptr_t kRoot = 0x7ff500100000ull, kChild = 0x7ff500200000ull;
constexpr uint32_t kValue = 0x06455A50u;
} // namespace

TEST(EldenRingHandleScan, AValueInTheRootItselfIsReportedWithItsOffset) {
    Mem m;
    m.region(kRoot, 0x200);
    m.put<uint32_t>(kRoot + 0x40, kValue);
    const auto hits = findValue(m, {{"player", kRoot, 0x200}}, kValue);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], "player+0x40");
}

TEST(EldenRingHandleScan, AValueOneLevelDownThroughAPointerIsReported) {
    Mem m;
    m.region(kRoot, 0x200);
    m.region(kChild, 0x200);
    m.put<uint64_t>(kRoot + 0x20, kChild);
    m.put<uint32_t>(kChild + 0x10, kValue);
    const auto hits = findValue(m, {{"player", kRoot, 0x200}}, kValue);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], "[player+0x20]+0x10");
}

TEST(EldenRingHandleScan, UnreadablePointersAndAbsentValuesGiveNothing) {
    Mem m;
    m.region(kRoot, 0x200);
    m.put<uint64_t>(kRoot + 0x20, 0x7ff5dead0000ull); // not mapped
    m.put<uint64_t>(kRoot + 0x28, 0x1234);             // not a user pointer
    EXPECT_TRUE(findValue(m, {{"player", kRoot, 0x200}}, kValue).empty());
    EXPECT_TRUE(findValue(m, {{"nowhere", 0x1000, 0x200}}, kValue).empty());
}

TEST(EldenRingHandleScan, TheHitListIsCapped) {
    Mem m;
    m.region(kRoot, 0x400);
    for (int i = 0; i < 50; ++i) m.put<uint32_t>(kRoot + 4 * i, kValue);
    EXPECT_EQ(findValue(m, {{"player", kRoot, 0x400}}, kValue, 0x200, 8).size(), 8u);
}

TEST(EldenRingHandleScan, ASharedChildIsScannedOnce) {
    Mem m;
    m.region(kRoot, 0x200);
    m.region(kChild, 0x200);
    m.put<uint64_t>(kRoot + 0x20, kChild);
    m.put<uint64_t>(kRoot + 0x30, kChild);
    m.put<uint32_t>(kChild + 0x10, kValue);
    EXPECT_EQ(findValue(m, {{"player", kRoot, 0x200}}, kValue).size(), 1u);
}
