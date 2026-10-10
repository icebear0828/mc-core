#include <gtest/gtest.h>

#include "mc/inventory.hpp"
#include "mc/inventory_layout.hpp"

using namespace mc;

TEST(Inventory, StackSizesFollowMinecraft) {
    EXPECT_EQ(maxStackSize(ItemId::DiamondSword), 1u);
    EXPECT_EQ(maxStackSize(ItemId::Bow), 1u);
    EXPECT_EQ(maxStackSize(ItemId::TotemOfUndying), 1u);
    EXPECT_EQ(maxStackSize(ItemId::EnderPearl), 16u);
    EXPECT_EQ(maxStackSize(ItemId::BlockDirt), 64u);
    EXPECT_EQ(maxStackSize(ItemId::Arrow), 64u);
    EXPECT_EQ(maxStackSize(ItemId::None), 0u);
}

TEST(Inventory, StartsEmptyWithThirtySixSlots) {
    Inventory inv;
    EXPECT_EQ(Inventory::kSlots, 36);
    for (int i = 0; i < Inventory::kSlots; ++i) EXPECT_TRUE(inv.slot(i).empty());
    EXPECT_TRUE(inv.cursor().empty());
}

TEST(Inventory, AddFillsExistingStacksThenHotbarThenMain) {
    Inventory inv;
    EXPECT_EQ(inv.add({ItemId::BlockDirt, 100}), 0u);
    EXPECT_EQ(inv.slot(0).count, 64u);
    EXPECT_EQ(inv.slot(1).count, 36u);
    EXPECT_EQ(inv.add({ItemId::BlockDirt, 40}), 0u);
    EXPECT_EQ(inv.slot(1).count, 64u);
    EXPECT_EQ(inv.slot(2).count, 12u);
    EXPECT_EQ(inv.slot(2).item, ItemId::BlockDirt);
}

TEST(Inventory, AddOverflowsIntoMainAndReportsLeftover) {
    Inventory inv;
    for (int i = 0; i < 9; ++i) inv.setSlot(i, {ItemId::DiamondSword, 1});
    EXPECT_EQ(inv.add({ItemId::BlockStone, 64}), 0u);
    EXPECT_EQ(inv.slot(9).item, ItemId::BlockStone); // first main slot
    for (int i = 9; i < 36; ++i) inv.setSlot(i, {ItemId::DiamondSword, 1});
    EXPECT_EQ(inv.add({ItemId::BlockStone, 5}), 5u);
}

TEST(Inventory, ConsumeAndHasAndConsumeFirst) {
    Inventory inv;
    inv.setSlot(3, {ItemId::GoldenApple, 2});
    inv.setSlot(20, {ItemId::TotemOfUndying, 1});
    EXPECT_TRUE(inv.has(ItemId::TotemOfUndying)); // anywhere, also the main inventory
    EXPECT_TRUE(inv.consume(3));
    EXPECT_EQ(inv.slot(3).count, 1u);
    EXPECT_TRUE(inv.consume(3));
    EXPECT_TRUE(inv.slot(3).empty());
    EXPECT_EQ(inv.slot(3).item, ItemId::None);
    EXPECT_FALSE(inv.consume(3));
    EXPECT_TRUE(inv.consumeFirst(ItemId::TotemOfUndying));
    EXPECT_FALSE(inv.has(ItemId::TotemOfUndying));
    EXPECT_FALSE(inv.consumeFirst(ItemId::TotemOfUndying));
}

TEST(Inventory, LeftClickPicksPlacesMergesAndSwaps) {
    Inventory inv;
    inv.setSlot(0, {ItemId::BlockDirt, 30});
    inv.setSlot(1, {ItemId::BlockDirt, 50});
    inv.setSlot(2, {ItemId::BlockStone, 7});
    inv.click(0, Button::Left);
    EXPECT_EQ(inv.cursor().count, 30u);
    EXPECT_TRUE(inv.slot(0).empty());
    inv.click(5, Button::Left); // empty slot: place all
    EXPECT_TRUE(inv.cursor().empty());
    EXPECT_EQ(inv.slot(5).count, 30u);
    inv.click(5, Button::Left);
    inv.click(1, Button::Left); // merge up to 64: 50 + 14, 16 stay on the cursor
    EXPECT_EQ(inv.slot(1).count, 64u);
    EXPECT_EQ(inv.cursor().count, 16u);
    inv.click(2, Button::Left); // different item: swap
    EXPECT_EQ(inv.slot(2).item, ItemId::BlockDirt);
    EXPECT_EQ(inv.slot(2).count, 16u);
    EXPECT_EQ(inv.cursor().item, ItemId::BlockStone);
    EXPECT_EQ(inv.cursor().count, 7u);
}

TEST(Inventory, RightClickTakesHalfAndPlacesOne) {
    Inventory inv;
    inv.setSlot(0, {ItemId::BlockDirt, 5});
    inv.click(0, Button::Right); // picks the larger half
    EXPECT_EQ(inv.cursor().count, 3u);
    EXPECT_EQ(inv.slot(0).count, 2u);
    inv.click(4, Button::Right); // empty slot: one item
    EXPECT_EQ(inv.slot(4).count, 1u);
    EXPECT_EQ(inv.cursor().count, 2u);
    inv.click(4, Button::Right); // same item: one more
    EXPECT_EQ(inv.slot(4).count, 2u);
    EXPECT_EQ(inv.cursor().count, 1u);
    inv.click(4, Button::Right);
    EXPECT_TRUE(inv.cursor().empty());
    EXPECT_EQ(inv.slot(4).count, 3u);
}

TEST(Inventory, RightClickDoesNotOverfillAStack) {
    Inventory inv;
    inv.setSlot(0, {ItemId::DiamondSword, 1});
    inv.setSlot(1, {ItemId::DiamondSword, 1});
    inv.click(0, Button::Left);
    inv.click(1, Button::Right); // unstackable: swap, nothing is lost or merged
    EXPECT_EQ(inv.slot(1).count, 1u);
    EXPECT_EQ(inv.cursor().count, 1u);
}

TEST(Inventory, ShiftClickMovesBetweenHotbarAndMain) {
    Inventory inv;
    inv.setSlot(2, {ItemId::BlockDirt, 10});
    inv.click(2, Button::Left, true);
    EXPECT_TRUE(inv.slot(2).empty());
    EXPECT_EQ(inv.slot(9).count, 10u);
    inv.setSlot(10, {ItemId::BlockDirt, 60});
    inv.click(9, Button::Left, true); // goes to the hotbar, merging into any partial stack there
    EXPECT_EQ(inv.slot(0).count, 10u);
    inv.click(10, Button::Left, true);
    EXPECT_EQ(inv.slot(0).count, 64u);
    EXPECT_EQ(inv.slot(1).count, 6u);
}

TEST(Inventory, ShiftClickLeavesTheRestWhenTheOtherSideIsFull) {
    Inventory inv;
    for (int i = 9; i < 36; ++i) inv.setSlot(i, {ItemId::DiamondSword, 1});
    inv.setSlot(0, {ItemId::BlockDirt, 10});
    inv.click(0, Button::Left, true);
    EXPECT_EQ(inv.slot(0).count, 10u);
}

TEST(Inventory, ClickingOutsideDeletesTheCursorStack) {
    Inventory inv;
    inv.setSlot(0, {ItemId::BlockDirt, 10});
    inv.click(0, Button::Left);
    inv.clickOutside();
    EXPECT_TRUE(inv.cursor().empty());
}

TEST(Inventory, PaletteGivesStacksLikeTheCreativeMenu) {
    Inventory inv;
    inv.clickPalette(ItemId::BlockDirt, Button::Left);
    EXPECT_EQ(inv.cursor().count, 64u);
    inv.clickPalette(ItemId::BlockDirt, Button::Left); // already full
    EXPECT_EQ(inv.cursor().count, 64u);
    inv.clickPalette(ItemId::BlockStone, Button::Left); // replaces what is on the cursor
    EXPECT_EQ(inv.cursor().item, ItemId::BlockStone);
    inv.clickOutside();
    inv.clickPalette(ItemId::EnderPearl, Button::Right); // one at a time
    inv.clickPalette(ItemId::EnderPearl, Button::Right);
    EXPECT_EQ(inv.cursor().count, 2u);
    inv.clickOutside();
    inv.clickPalette(ItemId::Arrow, Button::Left, true); // shift: a full stack straight into the inventory
    EXPECT_TRUE(inv.cursor().empty());
    EXPECT_EQ(inv.slot(0).item, ItemId::Arrow);
    EXPECT_EQ(inv.slot(0).count, 64u);
}

TEST(Inventory, NumberKeySwapsHoveredSlotWithHotbar) {
    Inventory inv;
    inv.setSlot(12, {ItemId::Bow, 1});
    inv.setSlot(3, {ItemId::BlockDirt, 5});
    inv.swapWithHotbar(12, 3);
    EXPECT_EQ(inv.slot(3).item, ItemId::Bow);
    EXPECT_EQ(inv.slot(12).item, ItemId::BlockDirt);
    inv.swapWithHotbar(3, 3); // same slot: no-op
    EXPECT_EQ(inv.slot(3).item, ItemId::Bow);
    inv.swapWithHotbar(40, 3); // out of range: ignored
    EXPECT_EQ(inv.slot(3).item, ItemId::Bow);
}

TEST(Inventory, DefaultHotbarMatchesTheOldController) {
    Inventory inv = Inventory::withDefaultHotbar();
    EXPECT_EQ(inv.slot(0).item, ItemId::DiamondSword);
    EXPECT_EQ(inv.slot(2).count, 64u);
    EXPECT_EQ(inv.slot(4).count, 16u);
    EXPECT_EQ(inv.slot(5).count, 8u);
    EXPECT_EQ(inv.slot(8).item, ItemId::TotemOfUndying);
}

TEST(Inventory, TheStartInventoryHasArrowsSoTheBowCanBeDrawnInSurvival) {
    Inventory inv = Inventory::withDefaultHotbar();
    EXPECT_EQ(inv.slot(9).item, ItemId::Arrow); // the first main slot, the hotbar is full
    EXPECT_EQ(inv.slot(9).count, 64u);
    EXPECT_TRUE(inv.has(ItemId::Arrow));
    EXPECT_TRUE(inv.consumeFirst(ItemId::Arrow));
    EXPECT_EQ(inv.slot(9).count, 63u);
}

TEST(Inventory, ReturnCursorPutsTheStackBackAndDropsWhatDoesNotFit) {
    Inventory inv;
    inv.setSlot(0, {ItemId::BlockDirt, 10});
    inv.click(0, Button::Left);
    inv.returnCursor();
    EXPECT_TRUE(inv.cursor().empty());
    EXPECT_EQ(inv.slot(0).count, 10u);
    for (int i = 0; i < 36; ++i) inv.setSlot(i, {ItemId::DiamondSword, 1});
    inv.click(5, Button::Left);
    inv.setSlot(5, {ItemId::DiamondSword, 1}); // the cursor now holds a second sword and there is no room
    inv.returnCursor();
    EXPECT_TRUE(inv.cursor().empty()); // lost, as an item dropped out of a full inventory
}

TEST(ZombieSpawnEgg, StacksTo64AndIsOfferedByThePalette) {
    EXPECT_EQ(maxStackSize(ItemId::ZombieSpawnEgg), 64u);
    bool found = false;
    for (ItemId id : paletteItems()) found = found || id == ItemId::ZombieSpawnEgg;
    EXPECT_TRUE(found);
    EXPECT_LE(paletteItems().size(), static_cast<size_t>(InventoryLayout::kPaletteSlots));
}
