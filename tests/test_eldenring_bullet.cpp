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
    EXPECT_EQ(f.row_id, 0xFFFFFFFFFFFFFFFFull);
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
    p.row_id = 0xFFFFFFFFFFFFFFFFull;
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
    EXPECT_EQ(f.row_id, 0xFFFFFFFFFFFFFFFFull);
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

TEST(EldenRingBulletFire, ATemplateIsUsableOnlyWithTheFullBodyAndARealParamRowAndBulletId) {
    auto t = realTemplate();
    EXPECT_TRUE(templateUsable(t.data(), t.size()));
    EXPECT_FALSE(templateUsable(t.data(), kRequestBytes));        // the file must hold the whole 0x110 bytes
    EXPECT_FALSE(templateUsable(nullptr, 0));
    auto no_row = realTemplate();
    const uint32_t minus_one = 0xFFFFFFFFu; // +0x08 = -1: the game refuses it (REVERSE 35.3)
    std::memcpy(no_row.data() + 0x08, &minus_one, 4);
    EXPECT_FALSE(templateUsable(no_row.data(), no_row.size()));
    auto no_id = realTemplate();
    std::memcpy(no_id.data() + 0x1C, &minus_one, 4);
    EXPECT_FALSE(templateUsable(no_id.data(), no_id.size()));
}

TEST(EldenRingBulletFire, TheTemplateOnDiskIsRewrittenOnlyWhenTheParamRowOrTheBulletIdChanged) {
    const auto a = realTemplate();
    auto b = realTemplate();
    EXPECT_FALSE(templateChanged(a.data(), b.data()));
    const uint32_t other = 56u + 1;
    std::memcpy(b.data() + 0x1C, &other, 4);
    EXPECT_TRUE(templateChanged(a.data(), b.data()));
    auto c = realTemplate();
    const uint32_t row = 0x06453B10u;
    std::memcpy(c.data() + 0x08, &row, 4);
    EXPECT_TRUE(templateChanged(a.data(), c.data()));
    auto d = realTemplate();
    const float moved = 99.f; // the matrix is different on every shot and is not a reason to rewrite the file
    std::memcpy(d.data() + 0x80, &moved, 4);
    EXPECT_FALSE(templateChanged(a.data(), d.data()));
}

TEST(EldenRingBulletDamage, ABoltDoesMoreThanADiamondSwordHitAndScalesWithTheVictimsHealth) {
    // MC: a crossbow bolt does 9, a diamond sword 7. Same balance as the melee hits: a 7-damage sword hit is 5% of the maximum health.
    EXPECT_EQ(boltDamageEr(1000), 64);   // 1000 * 0.05 * 9 / 7
    EXPECT_GT(boltDamageEr(1000), 50);   // more than the sword's 50
    EXPECT_EQ(boltDamageEr(2000), 129);  // linear in the health
}

TEST(EldenRingBulletDamage, TheMcDamageOfTheShotScalesTheHit) {
    EXPECT_EQ(boltDamageEr(1000, 9.f), boltDamageEr(1000));
    EXPECT_EQ(boltDamageEr(1000, 4.5f), 32);           // half the damage, half the hit
    EXPECT_GT(boltDamageEr(1000, 9.f), boltDamageEr(1000, 3.f));
    EXPECT_EQ(boltDamageEr(1000, 0.f), 1);             // a landed hit is never zero
}

TEST(EldenRingBulletDamage, AnUnknownHealthGivesNoDamageAndAHitNeverRoundsToZero) {
    EXPECT_EQ(boltDamageEr(0), 0);
    EXPECT_EQ(boltDamageEr(-5), 0);
    EXPECT_EQ(boltDamageEr(3), 1);
}

TEST(EldenRingBulletDamage, OnlyAHitByTheShootersBoltRightAfterAShotIsReplaced) {
    const uint64_t player = 0x7FF500100000ull, other = 0x7FF500400000ull;
    // Measured (REVERSE 35.5): a bolt hit on an enemy is u8[DA] == 2 with the player as the attacker argument.
    EXPECT_TRUE(isPlayersBoltHit(2, player, player, other, kBoltBulletId, 500));
    EXPECT_FALSE(isPlayersBoltHit(1, player, player, other, kBoltBulletId, 500));         // a melee hit
    EXPECT_FALSE(isPlayersBoltHit(6, player, player, other, kBoltBulletId, 500));         // the kind of an arrow that hits the player
    EXPECT_FALSE(isPlayersBoltHit(2, other, player, other, kBoltBulletId, 500));          // somebody else's projectile
    EXPECT_FALSE(isPlayersBoltHit(2, player, player, player, kBoltBulletId, 500));        // the player is the victim
    EXPECT_FALSE(isPlayersBoltHit(2, player, 0, other, kBoltBulletId, 500));              // no player yet
    EXPECT_FALSE(isPlayersBoltHit(2, player, player, other, 1234u, 500));                 // the last bullet was a spell, not a bolt
    EXPECT_FALSE(isPlayersBoltHit(2, player, player, other, kBoltBulletId, 4000));        // too long ago to be this bolt
    EXPECT_TRUE(isPlayersBoltHit(2, player, player, other, kBoltBulletId, 3999));
}

TEST(EldenRingBulletFeedback, TheHitShowsHalfTheMcDamageInHeartsRoundedDown) {
    EXPECT_EQ(hitHearts(9.f), 4);     // Minecraft: floor(damage / 2) hearts
    EXPECT_EQ(hitHearts(6.f), 3);
    EXPECT_EQ(hitHearts(1.4f), 0);
    EXPECT_EQ(hitHearts(0.f), 0);
    EXPECT_EQ(hitHearts(-3.f), 0);
}

TEST(EldenRingBulletFeedback, OnlyAFullPowerShotIsCritical) {
    EXPECT_TRUE(isCriticalShot(9.f));      // a full draw (6 + the critical bonus) or a crossbow bolt
    EXPECT_TRUE(isCriticalShot(12.f));
    EXPECT_FALSE(isCriticalShot(5.8f));    // power 0.96
    EXPECT_FALSE(isCriticalShot(1.4f));
}

TEST(EldenRingBulletFire, TheAttachPointFieldAt1CIsTakenFromTheTemplateUnlessAskedOtherwise) {
    const auto t = realTemplate();
    FireParams p = params();
    uint32_t v = 7;
    const auto kept = buildFireRequest(t.data(), t.size(), p);
    std::memcpy(&v, kept.data() + 0x1C, 4);
    EXPECT_EQ(v, 56u);                       // the template's own value
    p.attach_poly = -1;                      // negative: the game then keeps our matrix instead of taking the shooter's bone (REVERSE 35.12)
    const auto none = buildFireRequest(t.data(), t.size(), p);
    std::memcpy(&v, none.data() + 0x1C, 4);
    EXPECT_EQ(v, 0xFFFFFFFFu);
    EXPECT_EQ(none[0x18], 0xCD);             // the neighbours are not touched
    EXPECT_EQ(none[0x20], 0xCD);
    p.attach_poly = 56;
    const auto same = buildFireRequest(t.data(), t.size(), p);
    std::memcpy(&v, same.data() + 0x1C, 4);
    EXPECT_EQ(v, 56u);
}
