#include <gtest/gtest.h>

#include "eldenring_bullet.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace eldenring::bullet;

namespace {
std::vector<uint8_t> request() {
    std::vector<uint8_t> b(kRequestBytes, 0);
    auto put32 = [&](size_t off, uint32_t v) { std::memcpy(b.data() + off, &v, 4); };
    auto put64 = [&](size_t off, uint64_t v) { std::memcpy(b.data() + off, &v, 8); };
    put64(0x00, 0xFFFFFFFF16F00000ull); // owner handle
    put64(0x08, 0xFFFFFFFFFFFFFFFFull); // target handle
    put32(0x10, 0);
    put32(0x14, 0xFFFFFFFFu);
    put32(0x18, 0xFFFFFFFFu);
    put32(0x1C, 20007000u);             // BulletParam id (as the accepted layout says)
    put32(0x44, 0x08);
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10.5f, 2.25f, -3.f, 1};
    std::memcpy(b.data() + 0x50, m, sizeof(m));
    return b;
}
} // namespace

TEST(EldenRingBullet, TheNamedFieldsAreReadAtTheAcceptedOffsets) {
    const auto b = request();
    const Fields f = decode(b.data(), b.size());
    ASSERT_TRUE(f.valid);
    EXPECT_EQ(f.owner, 0xFFFFFFFF16F00000ull);
    EXPECT_EQ(f.target, 0xFFFFFFFFFFFFFFFFull);
    EXPECT_EQ(f.id_at_1c, 20007000u);
    EXPECT_EQ(f.flags_at_44, 0x08u);
    EXPECT_FLOAT_EQ(f.position[0], 10.5f);
    EXPECT_FLOAT_EQ(f.position[1], 2.25f);
    EXPECT_FLOAT_EQ(f.position[2], -3.f);
    EXPECT_FLOAT_EQ(f.forward[2], 1.f);
}

TEST(EldenRingBullet, AShortBufferIsNotDecoded) {
    const std::vector<uint8_t> b(0x40, 0);
    EXPECT_FALSE(decode(b.data(), b.size()).valid);
    EXPECT_FALSE(decode(nullptr, 0).valid);
}

TEST(EldenRingBullet, TheHexDumpHasOneLinePerSixteenBytesWithOffsets) {
    const auto b = request();
    const auto lines = hexDump(b.data(), b.size());
    ASSERT_EQ(lines.size(), (kRequestBytes + 15) / 16); // 0xB8 is 11.5 lines: the tail is printed too
    EXPECT_EQ(lines[0].substr(0, 8), "+0x000: ");
    EXPECT_NE(lines[0].find("00 00 F0 16 FF FF FF FF"), std::string::npos); // owner, little endian
    EXPECT_EQ(lines[1].substr(0, 8), "+0x010: ");
    EXPECT_NE(lines[1].find("58 48 31 01"), std::string::npos);              // 20007000 = 0x01314858, little endian
}

TEST(EldenRingBullet, EveryDwordIsListedSoWhicheverFieldHoldsTheIdCanBeFound) {
    const auto b = request();
    const std::vector<int> where = findDword(b.data(), b.size(), 20007000u);
    ASSERT_EQ(where.size(), 1u);
    EXPECT_EQ(where[0], 0x1C);
    EXPECT_TRUE(findDword(b.data(), b.size(), 12345u).empty());
}

TEST(EldenRingBullet, TheLogBudgetStopsAfterTheLimit) {
    LogBudget budget(3);
    EXPECT_TRUE(budget.take());
    EXPECT_TRUE(budget.take());
    EXPECT_TRUE(budget.take());
    EXPECT_FALSE(budget.take());
    EXPECT_FALSE(budget.take());
}
