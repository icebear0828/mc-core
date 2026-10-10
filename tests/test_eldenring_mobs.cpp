#include <gtest/gtest.h>

#include "eldenring_mobs.hpp"

#include <cmath>

using namespace eldenring::mobs;

namespace {
MobSnapshot at(uintptr_t id, float x, float z) {
    MobSnapshot s;
    s.id = id;
    s.feet[0] = x;
    s.feet[2] = z;
    return s;
}
} // namespace

TEST(EldenRingMobs, EachSummonGetsItsOwnDrawWithTheMatricesAtItsFeet) {
    MobRegistry r;
    const auto draws = r.update(1.f / 60.f, {at(0x10, 1.f, 2.f), at(0x20, 5.f, 6.f)});
    ASSERT_EQ(draws.size(), 2u);
    EXPECT_EQ(draws[0].id, 0x10u);
    EXPECT_EQ(draws[1].id, 0x20u);
    // the head part (index 0) is lifted to head height above the feet: its matrix is not the identity and differs per mob
    const auto head = static_cast<size_t>(mc::StevePart::Head);
    EXPECT_NE(draws[0].parts[head].m[12], draws[1].parts[head].m[12]);
    EXPECT_EQ(r.tracked(), 2u);
}

TEST(EldenRingMobs, ANullIdIsIgnoredAndTheNumberOfMobsIsCapped) {
    MobRegistry r;
    std::vector<MobSnapshot> many;
    many.push_back(at(0, 0.f, 0.f));
    for (uintptr_t i = 1; i <= kMaxMobs + 5; ++i) many.push_back(at(i, static_cast<float>(i), 0.f));
    EXPECT_EQ(r.update(1.f / 60.f, many).size(), kMaxMobs);
}

TEST(EldenRingMobs, AMobWalksItsOwnLegsAndAStandingOneDoesNot) {
    MobRegistry r;
    float x = 0.f;
    std::vector<MobDraw> last;
    for (int i = 0; i < 40; ++i) {
        x += 4.f / 60.f; // 4 m/s along +X
        last = r.update(1.f / 60.f, {at(1, x, 0.f), at(2, 10.f, 0.f)});
    }
    const auto leg = static_cast<size_t>(mc::StevePart::RightLeg);
    const auto& walker = last[0].parts[leg];
    const auto& stander = last[1].parts[leg];
    bool differs = false;
    for (int k = 0; k < 16; ++k) differs = differs || std::fabs(walker.m[k] - stander.m[k]) > 1e-3f;
    EXPECT_TRUE(differs);
}

TEST(EldenRingMobs, AnEntryNotSeenForAWhileIsDropped) {
    MobRegistry r;
    r.update(1.f / 60.f, {at(1, 0.f, 0.f), at(2, 3.f, 0.f)});
    EXPECT_EQ(r.tracked(), 2u);
    for (int i = 0; i < kMissedFramesBeforeDrop; ++i) r.update(1.f / 60.f, {at(2, 3.f, 0.f)});
    EXPECT_EQ(r.tracked(), 2u); // 1 has been missing for exactly the limit, not over it
    r.update(1.f / 60.f, {at(2, 3.f, 0.f)});
    EXPECT_EQ(r.tracked(), 1u);
    r.clear();
    EXPECT_EQ(r.tracked(), 0u);
}
