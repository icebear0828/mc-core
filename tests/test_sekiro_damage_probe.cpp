#include <gtest/gtest.h>

#include "sekiro_damage_probe.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace sekiro::live;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr size_t kImageSize = 0x100000;

class FakeMemory : public IMemoryReader {
public:
    std::map<uintptr_t, std::vector<uint8_t>> regions;
    bool read(uintptr_t a, void* out, size_t n) const override {
        for (const auto& [start, bytes] : regions) {
            if (a >= start && a + n <= start + bytes.size()) {
                std::memcpy(out, bytes.data() + (a - start), n);
                return true;
            }
        }
        return false;
    }
    void put(uintptr_t a, const std::vector<uint8_t>& v) {
        auto& r = regions[a & ~0xFFFFull];
        if (r.empty()) r.resize(0x10000, 0);
        std::memcpy(r.data() + (a & 0xFFFF), v.data(), v.size());
    }
};

} // namespace

TEST(SekiroDamageProbeTest, OnlyHooksWhenThePrologueIsTheOneWeAnalysed) {
    FakeMemory mem;
    const uintptr_t at = kBase + kDealDamageRva;
    EXPECT_FALSE(prologueMatches(mem, kBase, kDealDamageRva, kDealDamagePrologue)) << "unreadable";
    mem.put(at, std::vector<uint8_t>(std::begin(kDealDamagePrologue), std::end(kDealDamagePrologue)));
    EXPECT_TRUE(prologueMatches(mem, kBase, kDealDamageRva, kDealDamagePrologue));
    std::vector<uint8_t> changed(std::begin(kDealDamagePrologue), std::end(kDealDamagePrologue));
    changed[3] ^= 0xFF; // another game version
    mem.put(at, changed);
    EXPECT_FALSE(prologueMatches(mem, kBase, kDealDamageRva, kDealDamagePrologue));
}

TEST(SekiroDamageProbeTest, DescribesTheKnownFieldsAndEveryNonZeroWord) {
    std::vector<uint8_t> data(0x200, 0);
    const float hp = 123.0f;
    const int32_t posture = 46, stagger = 2;
    std::memcpy(&data[0x24], &hp, 4);
    std::memcpy(&data[0x28], &posture, 4);
    std::memcpy(&data[0x54], &stagger, 4);
    FakeMemory mem;
    const std::string text = describeDamageData(data.data(), data.size(), mem, kBase, kImageSize);
    EXPECT_NE(text.find("+0x024"), std::string::npos) << text;
    EXPECT_NE(text.find("+0x028"), std::string::npos);
    EXPECT_NE(text.find("+0x054"), std::string::npos);
    EXPECT_NE(text.find("hp_damage"), std::string::npos);
    EXPECT_NE(text.find("posture_damage"), std::string::npos);
    EXPECT_NE(text.find("stagger_level"), std::string::npos);
    EXPECT_EQ(text.find("+0x100"), std::string::npos) << "zero words are not listed";
}

TEST(SekiroDamageProbeTest, MarksPointerLikeWordsSoDanglingReferencesCanBeSpotted) {
    std::vector<uint8_t> data(0x200, 0);
    const uint64_t heap = 0x7ff4f6e05490ull, image = kBase + 0x1234, small = 7;
    std::memcpy(&data[0x40], &heap, 8);
    std::memcpy(&data[0x48], &image, 8);
    std::memcpy(&data[0x50], &small, 8);
    FakeMemory mem;
    mem.put(heap, std::vector<uint8_t>(16, 0xAB)); // readable heap object
    const std::string text = describeDamageData(data.data(), data.size(), mem, kBase, kImageSize);
    EXPECT_NE(text.find("+0x040"), std::string::npos);
    EXPECT_NE(text.find("heap pointer (readable)"), std::string::npos) << text;
    EXPECT_NE(text.find("image pointer"), std::string::npos) << text;
    EXPECT_EQ(text.find("pointer", text.find("+0x050")), std::string::npos) << "a small integer is not a pointer";
}

TEST(SekiroDamageProbeTest, AnUnreadableHeapPointerIsFlaggedAsDangling) {
    std::vector<uint8_t> data(0x200, 0);
    const uint64_t heap = 0x7ff4deadbeefull;
    std::memcpy(&data[0x80], &heap, 8);
    FakeMemory mem;
    const std::string text = describeDamageData(data.data(), data.size(), mem, kBase, kImageSize);
    EXPECT_NE(text.find("heap pointer (NOT readable)"), std::string::npos) << text;
}
