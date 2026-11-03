#include "eldenring_enemy_snapshot.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using eldenring::live::EnemyInfo;
using eldenring::live::gameThreadEnemySnapshot;

namespace {

std::shared_ptr<const std::vector<EnemyInfo>> MakeList(uintptr_t chr) {
    auto list = std::make_shared<std::vector<EnemyInfo>>();
    EnemyInfo e{};
    e.chr = chr;
    e.hp = 100;
    list->push_back(e);
    return list;
}

void Reset() {
    gameThreadEnemySnapshot().publish(nullptr, 0);
}

} // namespace

TEST(EldenringEnemySnapshot, FreshIsNullBeforeAnythingIsPublished) {
    Reset();
    EXPECT_EQ(gameThreadEnemySnapshot().fresh(1000, 100), nullptr);
}

TEST(EldenringEnemySnapshot, PublishesAndReturnsTheList) {
    Reset();
    gameThreadEnemySnapshot().publish(MakeList(0x1234), 1000);
    const auto list = gameThreadEnemySnapshot().fresh(1050, 100);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->size(), 1u);
    EXPECT_EQ((*list)[0].chr, 0x1234u);
}

TEST(EldenringEnemySnapshot, StaleSnapshotIsRejected) {
    Reset();
    gameThreadEnemySnapshot().publish(MakeList(0x1234), 1000);
    EXPECT_EQ(gameThreadEnemySnapshot().fresh(1101, 100), nullptr);
}

TEST(EldenringEnemySnapshot, NewPublishReplacesTheOldOne) {
    Reset();
    gameThreadEnemySnapshot().publish(MakeList(0x1111), 1000);
    gameThreadEnemySnapshot().publish(MakeList(0x2222), 1010);
    const auto list = gameThreadEnemySnapshot().fresh(1020, 100);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->size(), 1u);
    EXPECT_EQ((*list)[0].chr, 0x2222u);
}

TEST(EldenringEnemySnapshot, ClockGoingBackwardsIsRejected) {
    Reset();
    gameThreadEnemySnapshot().publish(MakeList(0x1234), 5000);
    EXPECT_EQ(gameThreadEnemySnapshot().fresh(4000, 100), nullptr);
}
