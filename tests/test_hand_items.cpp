#include <gtest/gtest.h>

#include "mc/hand_items.hpp"

using mc::Hand;
using mc::HandItemInput;
using mc::HandItems;
using mc::HandUse;
using mc::ItemId;

namespace {

HandItemInput withItems(ItemId main, ItemId off) {
    HandItemInput in;
    in.main_item = main;
    in.off_item = off;
    return in;
}

} // namespace

TEST(HandItemsTest, PresentItemKeepsItsSpriteAndNoUse) {
    const HandItems hands = mc::planHandItems(withItems(ItemId::DiamondSword, ItemId::None));
    EXPECT_TRUE(hands.main.present);
    EXPECT_FALSE(hands.off.present);
    EXPECT_EQ(hands.main.use, HandUse::None);
    EXPECT_EQ(hands.main.shown, ItemId::DiamondSword);
    EXPECT_EQ(hands.main.model, mc::HandModel::Sprite);
}

TEST(HandItemsTest, OffhandBowDrawsWhenTheMainHandDoesNotClaimTheButton) {
    HandItemInput in = withItems(ItemId::DiamondSword, ItemId::Bow);
    in.bow_active = true;
    in.bow_charge = 0.7f;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.main.use, HandUse::None);
    EXPECT_EQ(hands.off.use, HandUse::BowDraw);
    EXPECT_FLOAT_EQ(hands.off.progress, 0.7f);
    EXPECT_EQ(hands.off.shown, ItemId::BowPulling1);
}

TEST(HandItemsTest, MainHandRightClickItemBlocksTheOffhandBow) {
    HandItemInput in = withItems(ItemId::EnderPearl, ItemId::Bow);
    in.bow_active = true;
    in.bow_charge = 0.8f;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.main.use, HandUse::None);
    EXPECT_EQ(hands.off.use, HandUse::None);
}

TEST(HandItemsTest, MainTridentClaimsTheButtonOverAnOffhandBow) {
    // The trident is a right-click item too: an offhand bow must not draw while it is held.
    HandItemInput in = withItems(ItemId::Trident, ItemId::Bow);
    in.bow_active = true;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.off.use, HandUse::None);
}

TEST(HandItemsTest, OffhandTridentOwnsTheCharge) {
    HandItemInput in = withItems(ItemId::DiamondSword, ItemId::Trident);
    in.trident_active = true;
    in.trident_charge = 0.6f;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.main.use, HandUse::None);
    EXPECT_EQ(hands.off.use, HandUse::TridentCharge);
    EXPECT_FLOAT_EQ(hands.off.progress, 0.6f);
    EXPECT_EQ(hands.off.model, mc::HandModel::Trident);
}

TEST(HandItemsTest, MainTridentChargeStaysOnTheMainHand) {
    HandItemInput in = withItems(ItemId::Trident, ItemId::None);
    in.trident_active = true;
    in.trident_charge = 0.9f;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.main.use, HandUse::TridentCharge);
    EXPECT_EQ(hands.main.model, mc::HandModel::Trident);
    EXPECT_EQ(hands.off.use, HandUse::None);
}

TEST(HandItemsTest, RiptideKeepsTheTridentHand) {
    HandItemInput in = withItems(ItemId::DiamondSword, ItemId::Trident);
    in.trident_active = true;
    in.riptide = true;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.off.use, HandUse::Riptide);
}

TEST(HandItemsTest, OffhandShieldRaisesTheOffhand) {
    HandItemInput in = withItems(ItemId::DiamondSword, ItemId::Shield);
    in.blocking = true;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.off.use, HandUse::Block);
    EXPECT_TRUE(hands.off.raised);
    EXPECT_EQ(hands.main.use, HandUse::None);
}

TEST(HandItemsTest, OffhandFoodEatsWhenTheMainHandIsBusyWithSomethingElse) {
    HandItemInput in = withItems(ItemId::DiamondSword, ItemId::Bread);
    in.eating = true;
    in.eat_progress = 0.4f;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.off.use, HandUse::Eat);
    EXPECT_FLOAT_EQ(hands.off.progress, 0.4f);
}

TEST(HandItemsTest, MainHandFoodWinsOverOffhandFood) {
    HandItemInput in = withItems(ItemId::Bread, ItemId::CookedBeef);
    in.eating = true;
    in.eat_progress = 0.5f;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.main.use, HandUse::Eat);
    EXPECT_EQ(hands.off.use, HandUse::None);
}

TEST(HandItemsTest, SwingBelongsToTheMainHand) {
    HandItemInput in = withItems(ItemId::DiamondSword, ItemId::Shield);
    in.swing = 0.5f;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.main.use, HandUse::Swing);
    EXPECT_FLOAT_EQ(hands.main.progress, 0.5f);
    EXPECT_EQ(hands.off.use, HandUse::None);
}

TEST(HandItemsTest, AnActiveUseSuppressesTheSwingOnThatHand) {
    HandItemInput in = withItems(ItemId::Bow, ItemId::None);
    in.bow_active = true;
    in.bow_charge = 0.3f;
    in.swing = 0.5f;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.main.use, HandUse::BowDraw);
}

TEST(HandItemsTest, PlaceableBlocksClaimTheRightClick) {
    EXPECT_TRUE(mc::claimsRightClick(ItemId::BlockCobblestone));
    EXPECT_TRUE(mc::claimsRightClick(ItemId::CraftingTable));
    EXPECT_TRUE(mc::claimsRightClick(ItemId::FireworkRocket));
    EXPECT_FALSE(mc::claimsRightClick(ItemId::DiamondSword));
    EXPECT_FALSE(mc::claimsRightClick(ItemId::DiamondPickaxe));
}

TEST(HandItemsTest, AFireworkInTheOffhandBoostsWhenTheMainHandDoesNotClaimTheButton) {
    EXPECT_EQ(mc::handForUse(ItemId::FireworkRocket, ItemId::DiamondSword, ItemId::FireworkRocket), Hand::Off);
    EXPECT_EQ(mc::handForUse(ItemId::FireworkRocket, ItemId::None, ItemId::FireworkRocket), Hand::Off);
    EXPECT_EQ(mc::handForUse(ItemId::FireworkRocket, ItemId::FireworkRocket, ItemId::DiamondSword), Hand::Main);
    // Food/bow/shield/block in the main hand claim the button, so the offhand rocket waits.
    EXPECT_EQ(mc::handForUse(ItemId::FireworkRocket, ItemId::Bread, ItemId::FireworkRocket), std::nullopt);
    EXPECT_EQ(mc::handForUse(ItemId::FireworkRocket, ItemId::None, ItemId::DiamondSword), std::nullopt);
}

TEST(HandItemsTest, HandForUseFollowsTheMainHandFirstRule) {
    EXPECT_EQ(mc::handForUse(ItemId::Trident, ItemId::Trident, ItemId::Bow), Hand::Main);
    EXPECT_EQ(mc::handForUse(ItemId::Trident, ItemId::DiamondSword, ItemId::Trident), Hand::Off);
    EXPECT_EQ(mc::handForUse(ItemId::Bow, ItemId::Shield, ItemId::Bow), std::nullopt);
    EXPECT_EQ(mc::handForUse(ItemId::Bread, ItemId::DiamondSword, ItemId::Bread), Hand::Off);
}

TEST(HandItemsTest, ItemModelClassifiesBlocksShieldsAndTridents) {
    const HandItems hands = mc::planHandItems(withItems(ItemId::BlockCobblestone, ItemId::Shield));
    EXPECT_EQ(hands.main.model, mc::HandModel::Block);
    EXPECT_EQ(hands.off.model, mc::HandModel::Shield);
    EXPECT_EQ(mc::planHandItems(withItems(ItemId::Trident, ItemId::DiamondSword)).main.model, mc::HandModel::Trident);
    EXPECT_EQ(mc::planHandItems(withItems(ItemId::DiamondSword, ItemId::None)).main.model, mc::HandModel::Sprite);
}

TEST(HandItemsTest, FindItemReturnsNullWhenNeitherHandHoldsIt) {
    const HandItems hands = mc::planHandItems(withItems(ItemId::DiamondSword, ItemId::None));
    EXPECT_EQ(hands.findItem(ItemId::Trident), nullptr);
    const HandItems with_trident = mc::planHandItems(withItems(ItemId::DiamondSword, ItemId::Trident));
    ASSERT_NE(with_trident.findItem(ItemId::Trident), nullptr);
    EXPECT_EQ(with_trident.findItem(ItemId::Trident)->hand, Hand::Off);
}

TEST(HandItemsTest, AnExplicitBowHandOverridesTheCategoryRule) {
    // The main hand holds a block but the placement could not start, so the offhand bow owns the
    // click. Presentation must follow the use, not "the block claims the button".
    HandItemInput in = withItems(ItemId::BlockDirt, ItemId::Bow);
    in.bow_active = true;
    in.bow_charge = 0.7f;
    in.bow_hand = Hand::Off;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.main.use, HandUse::None);
    EXPECT_EQ(hands.off.use, HandUse::BowDraw);
    EXPECT_FLOAT_EQ(hands.off.progress, 0.7f);
    EXPECT_EQ(hands.off.shown, ItemId::BowPulling1);
}

TEST(HandItemsTest, AnExplicitEatHandOverridesTheCategoryRule) {
    HandItemInput in = withItems(ItemId::BlockDirt, ItemId::Bread);
    in.eating = true;
    in.eat_progress = 0.4f;
    in.eat_hand = Hand::Off;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.off.use, HandUse::Eat);
    EXPECT_FLOAT_EQ(hands.off.progress, 0.4f);
}

TEST(HandItemsTest, AnExplicitShieldHandOverridesTheCategoryRule) {
    HandItemInput in = withItems(ItemId::BlockDirt, ItemId::Shield);
    in.blocking = true;
    in.shield_hand = Hand::Off;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.off.use, HandUse::Block);
    EXPECT_TRUE(hands.off.raised);
}

TEST(HandItemsTest, AnExplicitTridentHandOverridesTheCategoryRule) {
    HandItemInput in = withItems(ItemId::BlockDirt, ItemId::Trident);
    in.trident_active = true;
    in.trident_charge = 0.5f;
    in.trident_hand = Hand::Off;
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.off.use, HandUse::TridentCharge);
    EXPECT_FLOAT_EQ(hands.off.progress, 0.5f);
}

TEST(HandItemsTest, AForcedHandThatDoesNotHoldTheItemFallsBack) {
    // A stale forced hand must not animate an item that is not there.
    HandItemInput in = withItems(ItemId::BlockDirt, ItemId::Bow);
    in.bow_active = true;
    in.bow_hand = Hand::Main; // the main hand holds a block, not the bow
    const HandItems hands = mc::planHandItems(in);
    EXPECT_EQ(hands.main.use, HandUse::None);
    EXPECT_EQ(hands.off.use, HandUse::None);
}
