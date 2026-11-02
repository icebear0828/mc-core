#include <gtest/gtest.h>

#include "mc/inventory_persistence.hpp"

using namespace mc;

namespace {

void expectSameStack(const ItemStack& a, const ItemStack& b) {
    EXPECT_EQ(a.item, b.item);
    EXPECT_EQ(a.count, b.count);
}

} // namespace

TEST(InventoryPersistence, EveryItemIdHasAStableRoundTrippingSlug) {
    for (uint16_t id = 1; id <= static_cast<uint16_t>(kLastItemId); ++id) {
        const ItemId item = static_cast<ItemId>(id);
        const std::string_view slug = itemSlug(item);
        ASSERT_FALSE(slug.empty()) << "item id " << id << " has no save slug";
        EXPECT_EQ(itemFromSlug(slug), item);
    }
    EXPECT_TRUE(itemSlug(ItemId::None).empty());
    EXPECT_EQ(itemFromSlug(""), ItemId::None);
    EXPECT_EQ(itemFromSlug("not_an_item"), ItemId::None);
}

TEST(InventoryPersistence, RoundTripsSlotsArmorOffhandCursorSelectedAndGrid) {
    Inventory inv;
    inv.setSlot(0, {ItemId::DiamondSword, 1});
    inv.setSlot(5, {ItemId::BlockDirt, 64});
    inv.setSlot(35, {ItemId::Coal, 7});
    inv.setArmor(0, {ItemId::DiamondHelmet, 1});
    inv.setArmor(1, {ItemId::Elytra, 1});
    inv.setOffhand({ItemId::Shield, 1});
    inv.setCursor({ItemId::Stick, 3});
    CraftingGrid grid;
    grid.cells[0] = {ItemId::WoodPlanks, 4};
    grid.cells[4] = {ItemId::Stick, 2};

    const std::string text = serializeInventory(inv, 5, grid);
    EXPECT_NE(text.find("slot.5=block_dirt:64"), std::string::npos);
    EXPECT_NE(text.find("armor.1=elytra:1"), std::string::npos);
    EXPECT_NE(text.find("offhand=shield:1"), std::string::npos);
    EXPECT_NE(text.find("cursor=stick:3"), std::string::npos);
    EXPECT_NE(text.find("grid.4=stick:2"), std::string::npos);

    Inventory loaded;
    int selected = 0;
    CraftingGrid loaded_grid;
    const InventoryLoadResult r = deserializeInventory(text, loaded, selected, loaded_grid);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.skipped, 0);
    EXPECT_EQ(selected, 5);
    for (int i = 0; i < Inventory::kSlots; ++i) expectSameStack(loaded.slot(i), inv.slot(i));
    for (int i = 0; i < Inventory::kArmorSlots; ++i) expectSameStack(loaded.armor(i), inv.armor(i));
    expectSameStack(loaded.offhand(), inv.offhand());
    expectSameStack(loaded.cursor(), inv.cursor());
    for (int i = 0; i < kCraftCellCount; ++i) expectSameStack(loaded_grid.cells[static_cast<size_t>(i)], grid.cells[static_cast<size_t>(i)]);
}

TEST(InventoryPersistence, EmptySlotsAreOmittedAndAnEmptyInventoryRoundTrips) {
    Inventory inv;
    CraftingGrid grid;
    const std::string text = serializeInventory(inv, 0, grid);
    EXPECT_EQ(text.find("slot."), std::string::npos);
    EXPECT_EQ(text.find("offhand="), std::string::npos);
    EXPECT_EQ(text.find("cursor="), std::string::npos);

    Inventory loaded = Inventory::withDefaultHotbar();
    int selected = 7;
    CraftingGrid loaded_grid;
    const InventoryLoadResult r = deserializeInventory(text, loaded, selected, loaded_grid);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(selected, 0);
    for (int i = 0; i < Inventory::kSlots; ++i) EXPECT_TRUE(loaded.slot(i).empty());
    EXPECT_TRUE(loaded.offhand().empty());
}

TEST(InventoryPersistence, UnknownItemsAndMalformedLinesAreSkipped) {
    Inventory inv;
    int selected = 0;
    CraftingGrid grid;
    const std::string text =
        "mc-inventory 1\n"
        "slot.0=not_an_item:1\n"
        "slot.1=diamond_sword:zero\n"
        "slot.2=diamond_sword:1\n"
        "nonsense line\n"
        "slot.999=block_dirt:1\n";
    const InventoryLoadResult r = deserializeInventory(text, inv, selected, grid);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.loaded, 1);
    EXPECT_EQ(r.skipped, 3); // unknown slug, bad count, line without '='
    EXPECT_EQ(inv.slot(2).item, ItemId::DiamondSword);
}

TEST(InventoryPersistence, AMissingHeaderLeavesTheInventoryUntouched) {
    Inventory inv;
    inv.setSlot(3, {ItemId::Bow, 1});
    int selected = 4;
    CraftingGrid grid;
    grid.cells[0] = {ItemId::Apple, 1};
    const InventoryLoadResult r = deserializeInventory("slot.0=diamond_sword:1\n", inv, selected, grid);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(inv.slot(3).item, ItemId::Bow);
    EXPECT_EQ(selected, 4);
    EXPECT_EQ(grid.cells[0].item, ItemId::Apple);
}

TEST(InventoryPersistence, AnUnsupportedVersionLeavesTheInventoryUntouched) {
    Inventory inv;
    inv.setSlot(3, {ItemId::Bow, 1});
    int selected = 4;
    CraftingGrid grid;
    const InventoryLoadResult r = deserializeInventory("mc-inventory 99\nslot.0=diamond_sword:1\n", inv, selected, grid);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("99"), std::string::npos);
    EXPECT_EQ(inv.slot(3).item, ItemId::Bow);
}

TEST(InventoryPersistence, CountsAreClampedToTheStackLimit) {
    Inventory inv;
    int selected = 0;
    CraftingGrid grid;
    const InventoryLoadResult r = deserializeInventory(
        "mc-inventory 1\nslot.0=diamond_sword:99\nslot.1=block_dirt:999\nslot.2=ender_pearl:99\n", inv, selected, grid);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(inv.slot(0).count, 1u);   // unstackable
    EXPECT_EQ(inv.slot(1).count, 64u);  // normal stack
    EXPECT_EQ(inv.slot(2).count, 16u);  // ender pearl stack
}

TEST(InventoryPersistence, ArmorSlotMismatchIsSkipped) {
    Inventory inv;
    int selected = 0;
    CraftingGrid grid;
    const InventoryLoadResult r = deserializeInventory(
        "mc-inventory 1\narmor.0=block_dirt:1\narmor.1=diamond_chestplate:1\narmor.2=diamond_chestplate:1\n", inv, selected,
        grid);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(inv.armor(0).empty());
    EXPECT_EQ(inv.armor(1).item, ItemId::DiamondChestplate);
    EXPECT_TRUE(inv.armor(2).empty());
    EXPECT_EQ(r.skipped, 2);
}

TEST(InventoryPersistence, CrlfLineEndingsAndSelectedClampAreAccepted) {
    Inventory inv;
    int selected = 0;
    CraftingGrid grid;
    const InventoryLoadResult r =
        deserializeInventory("mc-inventory 1\r\nselected=42\r\nslot.0=shield:1\r\n", inv, selected, grid);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(selected, Inventory::kHotbar - 1);
    EXPECT_EQ(inv.slot(0).item, ItemId::Shield);
}
