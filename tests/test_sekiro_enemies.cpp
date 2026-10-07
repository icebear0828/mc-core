#include <gtest/gtest.h>

#include "sekiro_enemies.hpp"

#include <algorithm>
#include <set>

using namespace sekiro::live;
using mc::EntityId;

namespace {

LiveEnemy enemy(uintptr_t handle, uint32_t char_id = 10010000, float x = 1.f, float hp = 2101.f, float max_hp = 2101.f) {
    LiveEnemy e;
    e.handle = handle;
    e.char_id = char_id;
    e.team = 5;
    e.hostile = true;
    e.position = {x, 1.f, 2.f};
    e.hp_valid = true;
    e.hp = hp;
    e.max_hp = max_hp;
    return e;
}

bool contains(const std::vector<EntityId>& v, EntityId id) { return std::find(v.begin(), v.end(), id) != v.end(); }

} // namespace

TEST(SekiroEnemyTrackerTest, GivesNewEnemiesFreshIdsAboveTheReservedOnes) {
    EnemyTracker t;
    const auto c = t.update({enemy(0x1000), enemy(0x2000)}, 0.016f);
    ASSERT_EQ(c.added.size(), 2u);
    EXPECT_GE(static_cast<uint64_t>(c.added[0]), 2u); // 0 = none, 1 = local player
    EXPECT_GE(static_cast<uint64_t>(c.added[1]), 2u);
    EXPECT_NE(c.added[0], c.added[1]);
    EXPECT_TRUE(c.removed.empty());
    EXPECT_EQ(t.count(), 2u);
}

TEST(SekiroEnemyTrackerTest, TheSameEnemyKeepsItsIdAcrossFramesAndTheMirrorFollowsTheGame) {
    EnemyTracker t;
    const EntityId id = t.update({enemy(0x1000, 10010000, 1.f, 2101.f)}, 0.016f).added.at(0);
    const auto c = t.update({enemy(0x1000, 10010000, 3.5f, 1800.f)}, 0.016f);
    EXPECT_TRUE(c.added.empty());
    EXPECT_TRUE(c.removed.empty());
    const auto* chr = t.mirror(id);
    ASSERT_NE(chr, nullptr);
    EXPECT_FLOAT_EQ(chr->Position.X, 3.5f);
    EXPECT_FLOAT_EQ(chr->Health, 1800.f);
    EXPECT_FLOAT_EQ(chr->MaxHealth, 2101.f);
    EXPECT_EQ(chr->TeamId, 1u); // enemy
    EXPECT_FALSE(chr->bIsDead);
}

TEST(SekiroEnemyTrackerTest, AnEnemyThatVanishesIsRemovedAndItsIdIsNeverReused) {
    EnemyTracker t;
    const EntityId first = t.update({enemy(0x1000)}, 0.016f).added.at(0);
    const auto gone = t.update({}, 0.016f);
    ASSERT_EQ(gone.removed.size(), 1u);
    EXPECT_EQ(gone.removed[0], first);
    EXPECT_EQ(t.mirror(first), nullptr);

    // the game reuses the same address for a different enemy: a different entity, a different id
    const EntityId second = t.update({enemy(0x1000)}, 0.016f).added.at(0);
    EXPECT_NE(second, first);
}

TEST(SekiroEnemyTrackerTest, TheSameAddressWithAnotherCharacterIdIsANewEntity) {
    EnemyTracker t;
    const EntityId a = t.update({enemy(0x1000, 10010000)}, 0.016f).added.at(0);
    const auto c = t.update({enemy(0x1000, 10100300)}, 0.016f); // slot recycled between two frames
    ASSERT_EQ(c.added.size(), 1u);
    ASSERT_EQ(c.removed.size(), 1u);
    EXPECT_EQ(c.removed[0], a);
    EXPECT_NE(c.added[0], a);
}

TEST(SekiroEnemyTrackerTest, OnlyLivingHostileEnemiesWithKnownHealthAreTargets) {
    EnemyTracker t;
    LiveEnemy neutral = enemy(0x2000);
    neutral.hostile = false;
    neutral.team = 9;
    LiveEnemy dead = enemy(0x3000);
    dead.hp = 0;
    dead.dead = true;
    LiveEnemy unknown_hp = enemy(0x4000);
    unknown_hp.hp_valid = false;
    const auto c = t.update({enemy(0x1000), neutral, dead, unknown_hp}, 0.016f);
    EXPECT_EQ(c.added.size(), 1u);
    EXPECT_EQ(t.count(), 1u);
}

TEST(SekiroEnemyTrackerTest, DyingRemovesTheTargetOnTheNextUpdate) {
    EnemyTracker t;
    const EntityId id = t.update({enemy(0x1000)}, 0.016f).added.at(0);
    LiveEnemy corpse = enemy(0x1000);
    corpse.hp = 0;
    corpse.dead = true;
    const auto c = t.update({corpse}, 0.016f);
    ASSERT_EQ(c.removed.size(), 1u);
    EXPECT_EQ(c.removed[0], id);
    EXPECT_EQ(t.count(), 0u);
}

TEST(SekiroEnemyTrackerTest, MirrorPointersStayValidWhileOtherEnemiesComeAndGo) {
    EnemyTracker t;
    const auto ids = t.update({enemy(0x1000), enemy(0x2000)}, 0.016f).added;
    sekiro::native::ChrIns* first = t.mirror(ids[0]);
    for (uintptr_t h = 0x3000; h < 0x3000 + 0x100 * 20; h += 0x100) t.update({enemy(0x1000), enemy(h)}, 0.016f);
    EXPECT_EQ(t.mirror(ids[0]), first) << "the adapter holds this pointer";
}

TEST(SekiroEnemyTrackerTest, HandleLookupGivesTheHostAddressOnlyForLiveTargets) {
    EnemyTracker t;
    const EntityId id = t.update({enemy(0xABC0)}, 0.016f).added.at(0);
    EXPECT_EQ(t.handleOf(id), 0xABC0u);
    EXPECT_EQ(t.handleOf(EntityId::LocalPlayer), 0u);
    EXPECT_EQ(t.handleOf(static_cast<EntityId>(999)), 0u);
}

TEST(SekiroEnemyTrackerTest, VelocityIsDifferencedPerEnemy) {
    EnemyTracker t;
    const EntityId id = t.update({enemy(0x1000, 10010000, 1.f)}, 0.1f).added.at(0);
    t.update({enemy(0x1000, 10010000, 1.5f)}, 0.1f); // 0.5 m in 0.1 s
    EXPECT_NEAR(t.mirror(id)->Velocity.X, 5.f, 1e-3f);
}

TEST(SekiroEnemyTrackerTest, ClearForgetsEveryoneAndReportsThemRemoved) {
    EnemyTracker t;
    const auto ids = t.update({enemy(0x1000), enemy(0x2000)}, 0.016f).added;
    const auto removed = t.clear();
    EXPECT_EQ(removed.size(), 2u);
    EXPECT_TRUE(contains(removed, ids[0]) && contains(removed, ids[1]));
    EXPECT_EQ(t.count(), 0u);
}
