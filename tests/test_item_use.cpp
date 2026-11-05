#include <gtest/gtest.h>

#include "mc/item_use.hpp"

using mc::Hand;
using mc::ItemId;
using mc::UseAvailability;
using mc::UseKind;
using mc::UseResolution;

namespace {

UseAvailability allAvailable() {
    UseAvailability a;
    a.place = a.eat = a.bow = a.shield = a.trident = true;
    a.ignite = a.throw_pearl = a.spawn_egg = a.firework = true;
    return a;
}

UseAvailability noneAvailable() { return {}; }

} // namespace

TEST(ItemUseTest, TheTableClassifiesEveryRightClickFamily) {
    EXPECT_EQ(mc::useSpec(ItemId::Bow).kind, UseKind::Bow);
    EXPECT_TRUE(mc::useSpec(ItemId::Bow).hold);
    EXPECT_EQ(mc::useSpec(ItemId::Trident).kind, UseKind::Trident);
    EXPECT_EQ(mc::useSpec(ItemId::Shield).kind, UseKind::Shield);
    EXPECT_EQ(mc::useSpec(ItemId::GoldenApple).kind, UseKind::Eat);
    EXPECT_EQ(mc::useSpec(ItemId::Bread).kind, UseKind::Eat);
    EXPECT_EQ(mc::useSpec(ItemId::FlintAndSteel).kind, UseKind::Ignite);
    EXPECT_EQ(mc::useSpec(ItemId::EnderPearl).kind, UseKind::ThrowPearl);
    EXPECT_EQ(mc::useSpec(ItemId::FireworkRocket).kind, UseKind::Firework);
    EXPECT_EQ(mc::useSpec(ItemId::ZombieSpawnEgg).kind, UseKind::SpawnEgg);
    EXPECT_EQ(mc::useSpec(ItemId::BlockCobblestone).kind, UseKind::Place);
    EXPECT_TRUE(mc::useSpec(ItemId::BlockCobblestone).repeats);
    EXPECT_EQ(mc::useSpec(ItemId::Torch).kind, UseKind::Place);
}

TEST(ItemUseTest, ItemsWithoutARightClickUseAreNone) {
    EXPECT_EQ(mc::useSpec(ItemId::None).kind, UseKind::None);
    EXPECT_EQ(mc::useSpec(ItemId::DiamondSword).kind, UseKind::None);
    EXPECT_EQ(mc::useSpec(ItemId::DiamondPickaxe).kind, UseKind::None);
    EXPECT_EQ(mc::useSpec(ItemId::Apple).kind, UseKind::None); // a crafting material here, not food
    EXPECT_EQ(mc::useSpec(ItemId::Elytra).kind, UseKind::None);
    EXPECT_FALSE(mc::claimsRightClick(ItemId::DiamondSword));
}

TEST(ItemUseTest, ClaimsRightClickMatchesTheTable) {
    EXPECT_TRUE(mc::claimsRightClick(ItemId::Bow));
    EXPECT_TRUE(mc::claimsRightClick(ItemId::BlockDirt));
    EXPECT_TRUE(mc::claimsRightClick(ItemId::Bread));
    EXPECT_TRUE(mc::claimsRightClick(ItemId::FlintAndSteel));
    EXPECT_TRUE(mc::claimsRightClick(ItemId::ZombieSpawnEgg));
    EXPECT_FALSE(mc::claimsRightClick(ItemId::DiamondSword));
    EXPECT_FALSE(mc::claimsRightClick(ItemId::None));
}

TEST(ItemUseTest, TheMainHandWinsWhenItCanStart) {
    const auto r = mc::resolveUse(ItemId::BlockDirt, ItemId::Bread, allAvailable(), allAvailable());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->hand, Hand::Main);
    EXPECT_EQ(r->kind, UseKind::Place);
}

TEST(ItemUseTest, TheOffhandActsWhenTheMainHandHasNoUse) {
    const auto r = mc::resolveUse(ItemId::DiamondSword, ItemId::BlockDirt, allAvailable(), allAvailable());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->hand, Hand::Off);
    EXPECT_EQ(r->kind, UseKind::Place);
}

TEST(ItemUseTest, TheOffhandActsWhenTheMainHandUseCannotStart) {
    // The main hand holds a block but the player is aiming at the sky: the placement does not
    // consume the click, so the offhand food is eaten.
    UseAvailability main = allAvailable();
    main.place = false;
    const auto r = mc::resolveUse(ItemId::BlockDirt, ItemId::Bread, main, allAvailable());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->hand, Hand::Off);
    EXPECT_EQ(r->kind, UseKind::Eat);
}

TEST(ItemUseTest, TheMainHandStillWinsWhenOnlyTheOffhandCanAct) {
    UseAvailability main = allAvailable();
    main.place = false;
    const auto r = mc::resolveUse(ItemId::Bow, ItemId::Bread, main, allAvailable());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->hand, Hand::Main);
    EXPECT_EQ(r->kind, UseKind::Bow);
}

TEST(ItemUseTest, NoHandActsWhenNothingCanStart) {
    EXPECT_FALSE(mc::resolveUse(ItemId::BlockDirt, ItemId::Bread, noneAvailable(), noneAvailable()).has_value());
    EXPECT_FALSE(mc::resolveUse(ItemId::DiamondSword, ItemId::None, allAvailable(), allAvailable()).has_value());
    EXPECT_FALSE(mc::resolveUse(ItemId::None, ItemId::None, allAvailable(), allAvailable()).has_value());
}

TEST(ItemUseTest, AnUnavailableOffhandUseDoesNotBlockTheMainHand) {
    UseAvailability off = allAvailable();
    off.bow = false; // no arrows
    const auto r = mc::resolveUse(ItemId::BlockDirt, ItemId::Bow, allAvailable(), off);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->hand, Hand::Main);
    EXPECT_EQ(r->kind, UseKind::Place);
}

TEST(ItemUseTest, HoldKindsAreExactlyTheFourPoseUses) {
    EXPECT_TRUE(mc::isHoldUse(UseKind::Eat));
    EXPECT_TRUE(mc::isHoldUse(UseKind::Bow));
    EXPECT_TRUE(mc::isHoldUse(UseKind::Shield));
    EXPECT_TRUE(mc::isHoldUse(UseKind::Trident));
    EXPECT_FALSE(mc::isHoldUse(UseKind::Place));
    EXPECT_FALSE(mc::isHoldUse(UseKind::Ignite));
    EXPECT_FALSE(mc::isHoldUse(UseKind::ThrowPearl));
    EXPECT_FALSE(mc::isHoldUse(UseKind::SpawnEgg));
    EXPECT_FALSE(mc::isHoldUse(UseKind::Firework));
    EXPECT_FALSE(mc::isHoldUse(UseKind::None));
}

TEST(ItemUseTest, ThePresentationPoseMatchesTheUseKind) {
    EXPECT_EQ(mc::useSpec(ItemId::Bow).presentation, mc::HandUse::BowDraw);
    EXPECT_EQ(mc::useSpec(ItemId::Trident).presentation, mc::HandUse::TridentCharge);
    EXPECT_EQ(mc::useSpec(ItemId::Shield).presentation, mc::HandUse::Block);
    EXPECT_EQ(mc::useSpec(ItemId::Bread).presentation, mc::HandUse::Eat);
    EXPECT_EQ(mc::useSpec(ItemId::BlockDirt).presentation, mc::HandUse::None);
    EXPECT_EQ(mc::useSpec(ItemId::EnderPearl).presentation, mc::HandUse::None);
}
