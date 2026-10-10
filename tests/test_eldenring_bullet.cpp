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

namespace {
FireParams params() {
    FireParams p;
    p.owner = 0xFFFFFFFF16F00000ull;
    p.target = 0xFFFFFFFFFFFFFFFFull;
    p.flags = 0x08;
    const float r[3] = {0.f, 0.f, -1.f}, u[3] = {0.f, 1.f, 0.f}, f[3] = {1.f, 0.f, 0.f}, pos[3] = {4.f, 5.f, 6.f};
    std::memcpy(p.right, r, 12);
    std::memcpy(p.up, u, 12);
    std::memcpy(p.forward, f, 12);
    std::memcpy(p.position, pos, 12);
    return p;
}
std::vector<uint8_t> realTemplate() {
    std::vector<uint8_t> b(kFullRequestBytes, 0xCD); // a recognisable filler: whatever the real shot had must survive
    auto put32 = [&](size_t off, uint32_t v) { std::memcpy(b.data() + off, &v, 4); };
    auto put64 = [&](size_t off, uint64_t v) { std::memcpy(b.data() + off, &v, 8); };
    put64(0x00, 0xFFFFFFFF16F00000ull);
    put64(0x08, 0xFFFFFFFF06455A50ull);
    put32(0x1C, 56u);
    put32(0x44, 0x09);
    put64(0xB0, 0x0000007647BEF600ull); // a stack pointer in the real request
    return b;
}
} // namespace

TEST(EldenRingBulletFire, TheRequestKeepsTheRealShotsBodyAndOverwritesOnlyTheFieldsWeOwn) {
    const auto t = realTemplate();
    const auto out = buildFireRequest(t.data(), t.size(), params());
    ASSERT_EQ(out.size(), kFullRequestBytes);
    const Fields f = decode(out.data(), out.size());
    EXPECT_EQ(f.owner, 0xFFFFFFFF16F00000ull);
    EXPECT_EQ(f.target, 0xFFFFFFFFFFFFFFFFull);   // free aim: no target
    EXPECT_EQ(f.id_at_1c, 56u);                   // the id of the real crossbow bolt survives
    EXPECT_EQ(f.flags_at_44, 0x08u);
    EXPECT_FLOAT_EQ(f.forward[0], 1.f);
    EXPECT_FLOAT_EQ(f.right[2], -1.f);
    EXPECT_FLOAT_EQ(f.position[0], 4.f);
    EXPECT_FLOAT_EQ(f.position[2], 6.f);
    EXPECT_EQ(out[0x30], 0xCD);                   // an untouched field of the template stays as the game wrote it
    EXPECT_EQ(out[0xA0], 0xCD);
}

TEST(EldenRingBulletFire, TheStackPointerOfTheTemplateIsNeverCopied) {
    const auto t = realTemplate();
    const auto out = buildFireRequest(t.data(), t.size(), params());
    uint64_t sub = 1;
    std::memcpy(&sub, out.data() + 0xB0, 8);
    EXPECT_EQ(sub, 0u);
}

TEST(EldenRingBulletFire, AnUnusableTemplateGivesNoRequest) {
    const std::vector<uint8_t> small(0x40, 0);
    EXPECT_TRUE(buildFireRequest(small.data(), small.size(), params()).empty());
    EXPECT_TRUE(buildFireRequest(nullptr, 0, params()).empty());
}

TEST(EldenRingBulletFire, ATemplateShorterThanTheFullBodyIsZeroExtended) {
    std::vector<uint8_t> t = realTemplate();
    t.resize(kRequestBytes); // what the logger writes first
    const auto out = buildFireRequest(t.data(), t.size(), params());
    ASSERT_EQ(out.size(), kFullRequestBytes);
    EXPECT_EQ(out[kRequestBytes], 0);
    EXPECT_EQ(decode(out.data(), out.size()).id_at_1c, 56u);
}

TEST(EldenRingBulletFire, TheMuzzleIsInFrontOfTheEyeAlongTheAim) {
    const float eye[3] = {1.f, 2.f, 3.f}, fwd[3] = {0.f, 0.f, 1.f};
    float out[3];
    muzzle(eye, fwd, 0.8f, out);
    EXPECT_FLOAT_EQ(out[0], 1.f);
    EXPECT_FLOAT_EQ(out[1], 2.f);
    EXPECT_FLOAT_EQ(out[2], 3.8f);
}

TEST(EldenRingBulletFire, OnlyAnInvalidHandleCountsAsAFailedSpawn) {
    EXPECT_TRUE(spawnFailed(0xFFFFFFFFu));
    EXPECT_FALSE(spawnFailed(0x0000FF00u));
    EXPECT_FALSE(spawnFailed(0x0003FF00u));
}

TEST(EldenRingBulletFire, TheVariantsSeparateTheTargetHandleFromTheHomingBitAndNeverHomeOntoThePlayer) {
    const uint64_t real = 0xFFFFFFFF06455A50ull, own = 0xFFFFFFFF16F00000ull;
    const unsigned n = variantCount();
    ASSERT_GE(n, 4u);
    bool has_target_only = false, has_bit_only = false, has_own_without_bit = false;
    for (unsigned i = 0; i < n; ++i) {
        const Variant v = variant(i, real, 0x09, own);
        EXPECT_FALSE(v.name.empty());
        EXPECT_TRUE(v.flags == 0x08u || v.flags == 0x09u);
        EXPECT_FALSE(v.target == own && (v.flags & 1u)); // a guided bolt that targets the shooter would fly back into him
        if (v.target == real && v.flags == 0x08u) has_target_only = true;
        if (v.target == 0xFFFFFFFFFFFFFFFFull && v.flags == 0x09u) has_bit_only = true;
        if (v.target == own && v.flags == 0x08u) has_own_without_bit = true;
    }
    EXPECT_TRUE(has_target_only);
    EXPECT_TRUE(has_bit_only);
    EXPECT_TRUE(has_own_without_bit);
}

TEST(EldenRingBulletFire, TheVariantIndexWrapsAround) {
    const uint64_t real = 1, own = 2;
    const unsigned n = variantCount();
    EXPECT_EQ(variant(0, real, 9, own).name, variant(n, real, 9, own).name);
    EXPECT_EQ(variant(1, real, 9, own).target, variant(n + 1, real, 9, own).target);
}
