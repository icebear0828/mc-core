#include <gtest/gtest.h>

#include "mc/crafting_session.hpp"
#include "mc/inventory.hpp"

using namespace mc;

namespace {

void placeOne(CraftingSession& s, Inventory& inv, ItemId id, int x, int y) {
    inv.setCursor({id, 1});
    s.clickGrid(x, y, Button::Right, false, inv);
    inv.setCursor({});
}

int countOf(const Inventory& inv, ItemId id) {
    int n = 0;
    for (int i = 0; i < Inventory::kSlots; ++i) {
        if (inv.slot(i).item == id) n += static_cast<int>(inv.slot(i).count);
    }
    if (inv.offhand().item == id) n += static_cast<int>(inv.offhand().count);
    return n;
}

} // namespace

TEST(CraftingSession, RightClickPlacesOneItemAndLeftClickPicksUpTheStack) {
    CraftingSession s;
    Inventory inv;
    placeOne(s, inv, ItemId::WoodPlanks, 0, 0);
    EXPECT_EQ(s.grid().at(0, 0).item, ItemId::WoodPlanks);
    EXPECT_EQ(s.grid().at(0, 0).count, 1u);

    inv.setCursor({ItemId::Stick, 7});
    s.clickGrid(1, 0, Button::Left, false, inv);
    EXPECT_EQ(s.grid().at(1, 0).item, ItemId::Stick);
    EXPECT_EQ(s.grid().at(1, 0).count, 7u);
    EXPECT_TRUE(inv.cursor().empty());
}

TEST(CraftingSession, SwordPatternMatchesAndCraftsAtomically) {
    CraftingSession s;
    Inventory inv;
    placeOne(s, inv, ItemId::WoodPlanks, 0, 0);
    placeOne(s, inv, ItemId::WoodPlanks, 0, 1);
    placeOne(s, inv, ItemId::Stick, 0, 2);
    ASSERT_TRUE(s.hasResult());
    EXPECT_EQ(s.resultStack().item, ItemId::WoodenSword);

    EXPECT_EQ(s.clickResult(Button::Left, false, inv), 1);
    EXPECT_TRUE(s.grid().empty());
    EXPECT_EQ(countOf(inv, ItemId::WoodenSword), 1);
    EXPECT_FALSE(s.hasResult());
}

TEST(CraftingSession, ShiftClickMovesTheGridStackToTheInventory) {
    CraftingSession s;
    Inventory inv;
    inv.setCursor({ItemId::IronIngot, 9});
    s.clickGrid(2, 2, Button::Left, false, inv);
    EXPECT_EQ(s.grid().at(2, 2).count, 9u);

    s.clickGrid(2, 2, Button::Left, true, inv);
    EXPECT_TRUE(s.grid().at(2, 2).empty());
    EXPECT_EQ(countOf(inv, ItemId::IronIngot), 9);
}

TEST(CraftingSession, ShiftClickResultCraftsAsManyAsTheGridAllows) {
    CraftingSession s;
    Inventory inv;
    inv.setCursor({ItemId::BlockWood, 5});
    s.clickGrid(0, 0, Button::Left, false, inv); // the whole stack of 5 logs in one cell
    ASSERT_TRUE(s.hasResult());
    EXPECT_EQ(s.resultStack().item, ItemId::WoodPlanks);

    EXPECT_EQ(s.clickResult(Button::Left, true, inv), 5);
    EXPECT_TRUE(s.grid().empty());
    EXPECT_EQ(countOf(inv, ItemId::WoodPlanks), 20);
}

TEST(CraftingSession, CraftFailsAtomicallyWhenTheInventoryCannotHoldTheResult) {
    CraftingSession s;
    Inventory inv;
    for (int i = 0; i < Inventory::kSlots; ++i) inv.setSlot(i, {ItemId::BlockDirt, 64});
    inv.setCursor({ItemId::BlockWood, 1});
    s.clickGrid(0, 0, Button::Left, false, inv);

    EXPECT_EQ(s.clickResult(Button::Left, false, inv), 0);
    EXPECT_EQ(s.grid().at(0, 0).item, ItemId::BlockWood);
    EXPECT_EQ(s.grid().at(0, 0).count, 1u);
}

TEST(CraftingSession, ReturnGridToEmptiesTheGridAndKeepsLeftoversWhenFull) {
    CraftingSession s;
    Inventory inv;
    placeOne(s, inv, ItemId::Diamond, 0, 0);
    placeOne(s, inv, ItemId::Emerald, 1, 1);
    s.returnGridTo(inv);
    EXPECT_TRUE(s.grid().empty());
    EXPECT_EQ(countOf(inv, ItemId::Diamond), 1);
    EXPECT_EQ(countOf(inv, ItemId::Emerald), 1);

    for (int i = 0; i < Inventory::kSlots; ++i) inv.setSlot(i, {ItemId::BlockDirt, 64});
    inv.setCursor({ItemId::Diamond, 3});
    s.clickGrid(0, 0, Button::Left, false, inv);
    s.returnGridTo(inv);
    EXPECT_EQ(s.grid().at(0, 0).item, ItemId::Diamond); // nothing was lost
    EXPECT_EQ(s.grid().at(0, 0).count, 3u);
}
