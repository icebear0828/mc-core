#include <gtest/gtest.h>

#include "eldenring_mobs.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

using namespace eldenring::mobs;

namespace {
MobSnapshot at(uintptr_t id, float x, float z) {
    MobSnapshot s;
    s.id = id;
    s.feet[0] = x;
    s.feet[2] = z;
    return s;
}
MobSnapshot withHp(uintptr_t id, int hp, int max_hp = 500) {
    MobSnapshot s = at(id, 1.f, 1.f);
    s.hp = hp;
    s.max_hp = max_hp;
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

namespace {
mc::model::EntityModel loadZombie() {
    std::ifstream file(std::string(MC_ASSET_DIR) + "/models/entities/zombie.geo.json");
    std::stringstream ss;
    ss << file.rdbuf();
    const auto m = mc::model::EntityModel::fromJson(ss.str());
    EXPECT_TRUE(m.has_value());
    return m.value_or(mc::model::EntityModel{});
}
size_t boneIx(const mc::model::EntityModel& m, const char* name) {
    for (size_t i = 0; i < m.bones.size(); ++i) {
        if (m.bones[i].name == name) return i;
    }
    ADD_FAILURE() << name;
    return 0;
}
} // namespace

TEST(EldenRingMobs, WithAModelFileEachMobGetsOneMatrixPerBone) {
    const auto zombie = loadZombie();
    MobRegistry r;
    r.setModel(&zombie);
    const auto draws = r.update(1.f / 60.f, {at(7, 2.f, 3.f)});
    ASSERT_EQ(draws.size(), 1u);
    EXPECT_TRUE(draws[0].generic);
    EXPECT_EQ(draws[0].bones.size(), zombie.bones.size());
}

TEST(EldenRingMobs, TheZombieHoldsItsArmsOutInFrontOfItsHeading) {
    const auto zombie = loadZombie();
    MobRegistry r;
    r.setModel(&zombie);
    for (const float yaw : {0.f, 1.5707963f}) {
        MobSnapshot s = at(1, 0.f, 0.f);
        s.yaw = yaw;
        std::vector<MobDraw> d;
        for (int i = 0; i < 3; ++i) d = r.update(1.f / 60.f, {s});
        const size_t arm = boneIx(zombie, "right_arm");
        const mc::Vec3 pivot = mc::model::bonePivotHost(zombie.bones[arm], eldenring::render::kBasis);
        const mc::Vec3 hand_rest = eldenring::render::kBasis.fromCanonical({0.f, -6.f * mc::rig::kCmPerModelPixel, 12.f * mc::rig::kCmPerModelPixel});
        const mc::Vec3 pivot_now = mc::rig::transformPoint(d[0].bones[arm], pivot);
        const mc::Vec3 hand = mc::rig::transformPoint(d[0].bones[arm], hand_rest);
        const mc::Vec3 dir = (hand - pivot_now).normalized();
        // the game's forward for a heading yaw is (sin yaw, 0, cos yaw)
        EXPECT_GT(dir.x * std::sin(yaw) + dir.z * std::cos(yaw), 0.9f) << yaw;
    }
}

TEST(EldenRingMobs, ZombieLegsSwingWhenItWalksAndTheArmsStayOut) {
    const auto zombie = loadZombie();
    MobRegistry r;
    r.setModel(&zombie);
    float x = 0.f;
    std::vector<MobDraw> d;
    const size_t leg = boneIx(zombie, "right_leg");
    const size_t arm = boneIx(zombie, "right_arm");
    float leg_travel = 0.f;
    mc::Vec3 foot_rest = eldenring::render::kBasis.fromCanonical({0.f, -2.f * mc::rig::kCmPerModelPixel, 0.f});
    for (int i = 0; i < 60; ++i) {
        x += 4.f / 60.f;
        d = r.update(1.f / 60.f, {at(1, 0.f, x)});
        // the foot moves back and forth along the walking direction (+Z) relative to the body
        const mc::Vec3 foot = mc::rig::transformPoint(d[0].bones[leg], foot_rest);
        leg_travel = std::max(leg_travel, std::fabs(foot.z - (foot_rest.z + x)));
    }
    EXPECT_GT(leg_travel, 0.15f);
    // the arm still points forward after a second of walking (no stride swing on top of the rest rotation)
    const mc::Vec3 pivot = mc::model::bonePivotHost(zombie.bones[arm], eldenring::render::kBasis);
    const mc::Vec3 hand_rest = eldenring::render::kBasis.fromCanonical({0.f, -6.f * mc::rig::kCmPerModelPixel, 12.f * mc::rig::kCmPerModelPixel});
    const mc::Vec3 dir = (mc::rig::transformPoint(d[0].bones[arm], hand_rest) - mc::rig::transformPoint(d[0].bones[arm], pivot)).normalized();
    EXPECT_GT(dir.z, 0.9f);
}

// ---- hurt flash and death -----------------------------------------------------------------------------------------------------
TEST(EldenRingMobs, ALossOfHitPointsFlashesRedForHalfASecondAndNothingElseDoes) {
    MobRegistry r;
    EXPECT_FLOAT_EQ(r.update(1.f / 60.f, {withHp(1, 500)})[0].hurt, 0.f);
    EXPECT_FLOAT_EQ(r.update(1.f / 60.f, {withHp(1, 500)})[0].hurt, 0.f);
    const auto hit = r.update(1.f / 60.f, {withHp(1, 430)});
    EXPECT_GT(hit[0].hurt, 0.9f);                                          // just hit
    float last = hit[0].hurt;
    int frames = 0;
    while (last > 0.f && frames < 200) {
        last = r.update(1.f / 60.f, {withHp(1, 430)})[0].hurt;
        ++frames;
    }
    EXPECT_NEAR(static_cast<float>(frames) / 60.f, 0.5f, 0.05f);          // 10 ticks
    EXPECT_FLOAT_EQ(r.update(1.f / 60.f, {withHp(1, 460)})[0].hurt, 0.f); // healing is not a hit
}

TEST(EldenRingMobs, UnknownHitPointsNeverHurtOrKill) {
    MobRegistry r;
    for (int i = 0; i < 5; ++i) {
        const auto d = r.update(1.f / 60.f, {at(1, 0.f, 0.f)}); // hp stays -1: not read
        EXPECT_FLOAT_EQ(d[0].hurt, 0.f);
    }
    EXPECT_EQ(r.tracked(), 1u);
}

TEST(EldenRingMobs, ADeadMobFallsOverRedAndIsGoneAfterAboutASecond) {
    MobRegistry r;
    r.update(1.f / 60.f, {withHp(1, 100)});
    auto d = r.update(1.f / 60.f, {withHp(1, 0)});
    EXPECT_GT(d[0].hurt, 0.9f);        // dying mobs stay red
    EXPECT_TRUE(d[0].dying);
    EXPECT_LT(d[0].fall, 0.2f);        // only just started to tip
    float fall = 0.f;
    for (int i = 0; i < 60; ++i) {
        d = r.update(1.f / 60.f, {withHp(1, 0)});
        fall = d[0].fall;
    }
    EXPECT_NEAR(fall, 1.f, 1e-4f);     // lying on its side after a second
    // still drawn lying there when the game has already removed the entity
    d = r.update(1.f / 60.f, {});
    EXPECT_TRUE(d.empty() || d[0].dying);
}

TEST(EldenRingMobs, AMobThatDiesKeepsLyingForItsDeathTimeEvenAfterTheGameRemovedIt) {
    MobRegistry r;
    r.update(1.f / 60.f, {withHp(1, 100)});
    r.update(1.f / 60.f, {withHp(1, 0)});   // first sight of 0 hp
    // the entity disappears on the very next frame
    std::vector<MobDraw> d;
    for (int i = 0; i < 20; ++i) d = r.update(1.f / 60.f, {});
    ASSERT_EQ(d.size(), 1u);               // 0.33 s later it still lies there
    EXPECT_TRUE(d[0].dying);
    EXPECT_EQ(d[0].id, 1u);
    for (int i = 0; i < 60; ++i) d = r.update(1.f / 60.f, {});
    EXPECT_TRUE(d.empty());                // and then it is gone
    EXPECT_EQ(r.tracked(), 0u);
}

TEST(EldenRingMobs, AMobThatSimplyVanishesAliveIsDroppedWithoutADeathScene) {
    MobRegistry r;
    r.update(1.f / 60.f, {withHp(1, 500)});
    std::vector<MobDraw> d;
    for (int i = 0; i < kMissedFramesBeforeDrop + 2; ++i) d = r.update(1.f / 60.f, {});
    EXPECT_TRUE(d.empty());
    EXPECT_EQ(r.tracked(), 0u);
}

TEST(EldenRingMobs, TheFallTurnsTheWholeFigureAboutItsFeet) {
    MobRegistry r;
    MobSnapshot alive = withHp(1, 100);
    alive.feet[0] = 5.f;
    alive.feet[2] = 7.f;
    r.update(1.f / 60.f, {alive});
    MobSnapshot dead = alive;
    dead.hp = 0;
    std::vector<MobDraw> d;
    for (int i = 0; i < 60; ++i) d = r.update(1.f / 60.f, {dead});
    const auto head = static_cast<size_t>(mc::StevePart::Head);
    // after the flip the head is no longer above the feet but about a body length to the side, at roughly knee height
    const mc::Vec3 top = mc::rig::transformPoint(d[0].parts[head], {0.f, 1.7f, 0.f}); // the matrices act on the rest pose with the feet at the origin
    EXPECT_LT(top.y, 0.6f);
    EXPECT_GT(std::fabs(top.x - 5.f) + std::fabs(top.z - 7.f), 1.0f);
}

TEST(EldenRingMobs, ACorpseTheGameKeepsInTheListIsDrawnForOneSecondOnly) {
    // 2026-10-10 log: the game keeps a dead summon (hp 0) in the list for about 7.6 s; the body is gone after kDeathSeconds anyway
    MobRegistry r;
    r.update(1.f / 60.f, {withHp(1, 100)});
    int drawn_frames = 0;
    for (int i = 0; i < 60 * 8; ++i) {
        const auto d = r.update(1.f / 60.f, {withHp(1, 0)});
        if (!d.empty()) ++drawn_frames;
    }
    EXPECT_NEAR(static_cast<float>(drawn_frames) / 60.f, kDeathSeconds, 0.1f);
    // the entry is still tracked while the game lists the corpse (so it is not drawn again), and goes with it
    EXPECT_EQ(r.tracked(), 1u);
    for (int i = 0; i < 5; ++i) r.update(1.f / 60.f, {});
    EXPECT_EQ(r.tracked(), 0u);
}

TEST(EldenRingMobs, AnotherMobIsStillDrawnWhileAnotherCorpseLies) {
    MobRegistry r;
    r.update(1.f / 60.f, {withHp(1, 100), withHp(2, 100)});
    std::vector<MobDraw> d;
    for (int i = 0; i < 120; ++i) d = r.update(1.f / 60.f, {withHp(1, 0), withHp(2, 100)});
    ASSERT_EQ(d.size(), 1u); // the corpse of 1 is gone after a second, the living 2 is still there
    EXPECT_EQ(d[0].id, 2u);
    EXPECT_FALSE(d[0].dying);
}
