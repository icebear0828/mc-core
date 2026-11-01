// Issue #67: the 3x3 crafting recipe engine. Pure logic, no game access.
//
// Covered here: shaped matching at any grid offset, trimming, horizontal mirror,
// shapeless multiset matching, strict consumption, atomic failure and output stacking.

#include <gtest/gtest.h>

#include "mc/hud.hpp"
#include "mc/inventory.hpp"
#include "mc/recipe.hpp"

#include <array>

using namespace mc;

namespace {

CraftingGrid grid3(ItemId a, ItemId b, ItemId c, ItemId d, ItemId e, ItemId f, ItemId g, ItemId h, ItemId i) {
    CraftingGrid grid;
    const std::array<ItemId, kCraftCellCount> cells{a, b, c, d, e, f, g, h, i};
    for (int i_cell = 0; i_cell < kCraftCellCount; ++i_cell) {
        const ItemId id = cells[static_cast<size_t>(i_cell)];
        if (id != ItemId::None) grid.cells[static_cast<size_t>(i_cell)] = {id, 1};
    }
    return grid;
}

// Lay a shaped recipe's pattern into the grid at (ox, oy), one item per cell.
CraftingGrid placePattern(const Recipe& r, int ox = 0, int oy = 0) {
    CraftingGrid grid;
    for (int py = 0; py < r.height; ++py) {
        for (int px = 0; px < r.width; ++px) {
            const ItemId id = r.pattern[static_cast<size_t>(py * r.width + px)];
            if (id != ItemId::None) grid.at(ox + px, oy + py) = {id, 1};
        }
    }
    return grid;
}

const Recipe* findResult(RecipeBook& book, ItemId result) {
    for (const Recipe& r : book.recipes()) {
        if (r.result.item == result) return &r;
    }
    return nullptr;
}

// An asymmetric 2x2 shaped recipe used for mirror tests.
Recipe asymmetricRecipe(bool mirror) {
    Recipe r;
    r.kind = RecipeKind::Shaped;
    r.category = RecipeCategory::Other;
    r.width = 2;
    r.height = 2;
    r.mirror = mirror;
    r.pattern[0] = ItemId::WoodPlanks; // top-left
    r.pattern[1] = ItemId::Stick;      // top-right
    r.pattern[2] = ItemId::IronIngot;  // bottom-left
    r.pattern[3] = ItemId::Diamond;    // bottom-right
    r.result = {ItemId::CraftingTable, 1};
    return r;
}

Inventory fullOfDirt() {
    Inventory inv;
    for (int i = 0; i < Inventory::kSlots; ++i) inv.setSlot(i, {ItemId::BlockDirt, 64});
    return inv;
}

} // namespace

// --- Sword ladder ---------------------------------------------------------

TEST(RecipeBook, DefaultRecipesCoverTheSwordLadder) {
    RecipeBook book;
    const std::array<ItemId, 5> ladder{ItemId::WoodenSword, ItemId::StoneSword, ItemId::IronSword, ItemId::DiamondSword,
                                       ItemId::NetheriteSword};
    for (ItemId item : ladder) {
        const Recipe* r = findResult(book, item);
        ASSERT_NE(r, nullptr) << "missing recipe for sword " << static_cast<int>(item);
        EXPECT_EQ(r->category, RecipeCategory::WeaponUpgrade);
        EXPECT_EQ(r->result.count, 1u);
    }
}

TEST(RecipeBook, SwordLadderRecipesMatchAndCraftInOrder) {
    RecipeBook book;
    const std::array<ItemId, 5> ladder{ItemId::WoodenSword, ItemId::StoneSword, ItemId::IronSword, ItemId::DiamondSword,
                                       ItemId::NetheriteSword};
    for (ItemId item : ladder) {
        const Recipe* r = findResult(book, item);
        ASSERT_NE(r, nullptr);
        CraftingGrid grid = placePattern(*r);
        EXPECT_TRUE(book.matches(*r, grid)) << "sword recipe did not match: " << static_cast<int>(item);
        EXPECT_EQ(book.find(grid), r);

        Inventory inv;
        const CraftResult out = book.craft(*r, grid, inv);
        ASSERT_TRUE(out.ok);
        EXPECT_EQ(out.output.item, item);
        EXPECT_TRUE(grid.empty()) << "craft must consume every occupied cell";
        EXPECT_EQ(inv.slot(0).item, item);
    }
}

// --- Tactical recipes -----------------------------------------------------

TEST(RecipeBook, ShieldAnvilTntFlintAndSteelAndGoldenAppleRecipesExist) {
    RecipeBook book;
    const std::array<ItemId, 5> items{ItemId::Shield, ItemId::Anvil, ItemId::BlockTnt, ItemId::FlintAndSteel,
                                      ItemId::GoldenApple};
    for (ItemId item : items) {
        const Recipe* r = findResult(book, item);
        ASSERT_NE(r, nullptr) << "missing recipe for item " << static_cast<int>(item);
        CraftingGrid grid;
        if (r->kind == RecipeKind::Shaped) {
            grid = placePattern(*r);
        } else {
            int i = 0;
            for (int k = 0; k < r->ingredient_count; ++k) {
                for (int n = 0; n < r->ingredients[static_cast<size_t>(k)].count; ++n) {
                    grid.cells[static_cast<size_t>(i++)] = {r->ingredients[static_cast<size_t>(k)].item, 1};
                }
            }
        }
        EXPECT_TRUE(book.matches(*r, grid)) << "recipe did not match: " << static_cast<int>(item);
        EXPECT_EQ(book.find(grid), r);
    }
}

// --- Shaped matching ------------------------------------------------------

TEST(RecipeBook, ShapedRecipeMatchesAnywhereInTheThreeByThreeGrid) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::WoodenSword);
    ASSERT_NE(r, nullptr);
    for (int ox = 0; ox < kCraftGridSize; ++ox) {
        const CraftingGrid grid = placePattern(*r, ox, 0);
        EXPECT_TRUE(book.matches(*r, grid)) << "offset x=" << ox;
        EXPECT_EQ(book.find(grid), r);
    }
    // A 1x1 pattern can sit in any of the nine cells.
    Recipe single;
    single.kind = RecipeKind::Shaped;
    single.width = 1;
    single.height = 1;
    single.pattern[0] = ItemId::Apple;
    single.result = {ItemId::Apple, 1};
    for (int y = 0; y < kCraftGridSize; ++y) {
        for (int x = 0; x < kCraftGridSize; ++x) {
            EXPECT_TRUE(book.matches(single, placePattern(single, x, y))) << "cell " << x << "," << y;
        }
    }
}

TEST(RecipeBook, ShapedRecipeRejectsExtraItemsOutsideThePattern) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::WoodenSword);
    ASSERT_NE(r, nullptr);
    CraftingGrid grid = placePattern(*r);
    grid.at(2, 2) = {ItemId::Apple, 1};
    EXPECT_FALSE(book.matches(*r, grid));
    EXPECT_EQ(book.find(grid), nullptr);
}

TEST(RecipeBook, ShapedRecipeRequiresInternalEmptyCellsToBeEmpty) {
    RecipeBook book;
    const Recipe* shield = findResult(book, ItemId::Shield);
    ASSERT_NE(shield, nullptr);
    ASSERT_EQ(shield->width, 3);
    ASSERT_EQ(shield->height, 3);
    // The vanilla shield pattern leaves the bottom-left cell empty.
    ASSERT_EQ(shield->pattern[static_cast<size_t>(2 * shield->width + 0)], ItemId::None);
    CraftingGrid grid = placePattern(*shield);
    grid.at(0, 2) = {ItemId::Apple, 1};
    EXPECT_FALSE(book.matches(*shield, grid));
}

TEST(RecipeBook, ShapedRecipeMatchesTheHorizontalMirrorWhenMirrorIsTrue) {
    RecipeBook book;
    const Recipe r = asymmetricRecipe(true);
    const CraftingGrid mirrored = grid3(ItemId::Stick, ItemId::WoodPlanks, ItemId::None, ItemId::Diamond, ItemId::IronIngot,
                                        ItemId::None, ItemId::None, ItemId::None, ItemId::None);
    EXPECT_TRUE(book.matches(r, mirrored));
}

TEST(RecipeBook, ShapedRecipeDoesNotMatchTheMirrorWhenMirrorIsFalse) {
    RecipeBook book;
    const Recipe r = asymmetricRecipe(false);
    const CraftingGrid mirrored = grid3(ItemId::Stick, ItemId::WoodPlanks, ItemId::None, ItemId::Diamond, ItemId::IronIngot,
                                        ItemId::None, ItemId::None, ItemId::None, ItemId::None);
    EXPECT_FALSE(book.matches(r, mirrored));
    EXPECT_TRUE(book.matches(r, grid3(ItemId::WoodPlanks, ItemId::Stick, ItemId::None, ItemId::IronIngot, ItemId::Diamond,
                                       ItemId::None, ItemId::None, ItemId::None, ItemId::None)));
}

TEST(RecipeBook, ShapedRecipeDoesNotMatchAVerticallyFlippedPattern) {
    RecipeBook book;
    const Recipe r = asymmetricRecipe(true);
    // Vertical flip only: rows swapped, columns kept.
    const CraftingGrid vertical = grid3(ItemId::IronIngot, ItemId::Diamond, ItemId::None, ItemId::WoodPlanks, ItemId::Stick,
                                        ItemId::None, ItemId::None, ItemId::None, ItemId::None);
    EXPECT_FALSE(book.matches(r, vertical));
}

// --- Shapeless matching ---------------------------------------------------

TEST(RecipeBook, ShapelessRecipeMatchesAnyOrder) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::FlintAndSteel);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(r->kind, RecipeKind::Shapeless);
    EXPECT_TRUE(book.matches(*r, grid3(ItemId::IronIngot, ItemId::Flint, ItemId::None, ItemId::None, ItemId::None,
                                       ItemId::None, ItemId::None, ItemId::None, ItemId::None)));
    EXPECT_TRUE(book.matches(*r, grid3(ItemId::Flint, ItemId::IronIngot, ItemId::None, ItemId::None, ItemId::None,
                                       ItemId::None, ItemId::None, ItemId::None, ItemId::None)));
    EXPECT_TRUE(book.matches(*r, grid3(ItemId::None, ItemId::None, ItemId::Flint, ItemId::None, ItemId::IronIngot,
                                       ItemId::None, ItemId::None, ItemId::None, ItemId::None)));
}

TEST(RecipeBook, ShapelessRecipeRequiresTheExactMultiset) {
    RecipeBook book;
    Recipe r;
    r.kind = RecipeKind::Shapeless;
    r.ingredients[0] = {ItemId::Stick, 2};
    r.ingredient_count = 1;
    r.result = {ItemId::WoodPlanks, 1};

    // Two sticks match.
    EXPECT_TRUE(book.matches(r, grid3(ItemId::Stick, ItemId::Stick, ItemId::None, ItemId::None, ItemId::None, ItemId::None,
                                      ItemId::None, ItemId::None, ItemId::None)));
    // One stick is not enough.
    EXPECT_FALSE(book.matches(r, grid3(ItemId::Stick, ItemId::None, ItemId::None, ItemId::None, ItemId::None, ItemId::None,
                                       ItemId::None, ItemId::None, ItemId::None)));
    // A stick plus a different item is not the right multiset.
    EXPECT_FALSE(book.matches(r, grid3(ItemId::Stick, ItemId::Apple, ItemId::None, ItemId::None, ItemId::None, ItemId::None,
                                       ItemId::None, ItemId::None, ItemId::None)));
}

TEST(RecipeBook, ShapelessRecipeRejectsExtraItems) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::FlintAndSteel);
    ASSERT_NE(r, nullptr);
    EXPECT_FALSE(book.matches(*r, grid3(ItemId::IronIngot, ItemId::Flint, ItemId::Apple, ItemId::None, ItemId::None,
                                        ItemId::None, ItemId::None, ItemId::None, ItemId::None)));
}

// --- Craft / consume / produce -------------------------------------------

TEST(RecipeBook, CraftConsumesOnePerOccupiedCellAndLeavesTheRestOfTheStack) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::FlintAndSteel);
    ASSERT_NE(r, nullptr);
    CraftingGrid grid;
    grid.at(0, 0) = {ItemId::IronIngot, 3};
    grid.at(1, 0) = {ItemId::Flint, 2};

    Inventory inv;
    const CraftResult out = book.craft(*r, grid, inv);
    ASSERT_TRUE(out.ok);
    EXPECT_EQ(grid.at(0, 0).item, ItemId::IronIngot);
    EXPECT_EQ(grid.at(0, 0).count, 2u);
    EXPECT_EQ(grid.at(1, 0).item, ItemId::Flint);
    EXPECT_EQ(grid.at(1, 0).count, 1u);
    EXPECT_EQ(inv.slot(0).item, ItemId::FlintAndSteel);
}

TEST(RecipeBook, CraftFailsAtomicallyWhenTheResultDoesNotFit) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::WoodPlanks);
    ASSERT_NE(r, nullptr);
    CraftingGrid grid;
    grid.at(0, 0) = {ItemId::BlockWood, 1};
    Inventory inv = fullOfDirt();

    const CraftResult out = book.craft(*r, grid, inv);
    EXPECT_FALSE(out.ok);
    EXPECT_EQ(grid.at(0, 0).item, ItemId::BlockWood) << "a failed craft must consume nothing";
    EXPECT_EQ(grid.at(0, 0).count, 1u);
}

TEST(RecipeBook, CraftFailsWhenTheGridDoesNotMatchAndConsumesNothing) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::WoodenSword);
    ASSERT_NE(r, nullptr);
    CraftingGrid grid = grid3(ItemId::WoodPlanks, ItemId::WoodPlanks, ItemId::None, ItemId::Apple, ItemId::None, ItemId::None,
                              ItemId::None, ItemId::None, ItemId::None);
    Inventory inv;
    const CraftResult out = book.craft(*r, grid, inv);
    EXPECT_FALSE(out.ok);
    EXPECT_EQ(grid.at(0, 0).item, ItemId::WoodPlanks);
    EXPECT_EQ(grid.at(1, 0).item, ItemId::WoodPlanks);
    EXPECT_TRUE(inv.slot(0).empty());
}

TEST(RecipeBook, CraftOutputStacksOntoAnExistingPartialStack) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::WoodPlanks);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(r->result.count, 4u);
    CraftingGrid grid;
    grid.at(0, 0) = {ItemId::BlockWood, 1};

    Inventory inv;
    inv.setSlot(0, {ItemId::WoodPlanks, 62});
    const CraftResult out = book.craft(*r, grid, inv);
    ASSERT_TRUE(out.ok);
    EXPECT_EQ(inv.slot(0).item, ItemId::WoodPlanks);
    EXPECT_EQ(inv.slot(0).count, 64u);
    EXPECT_EQ(inv.slot(1).item, ItemId::WoodPlanks);
    EXPECT_EQ(inv.slot(1).count, 2u);
}

TEST(RecipeBook, QuickCraftTakesIngredientsFromTheInventoryAndProducesTheResult) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::FlintAndSteel);
    ASSERT_NE(r, nullptr);
    Inventory inv;
    inv.setSlot(3, {ItemId::IronIngot, 1});
    inv.setSlot(20, {ItemId::Flint, 1});

    const CraftResult out = book.quickCraft(*r, inv);
    ASSERT_TRUE(out.ok);
    EXPECT_EQ(out.output.item, ItemId::FlintAndSteel);
    EXPECT_TRUE(inv.slot(3).empty());
    EXPECT_TRUE(inv.slot(20).empty());
    EXPECT_TRUE(inv.has(ItemId::FlintAndSteel));
}

TEST(RecipeBook, QuickCraftFailsWhenTheInventoryIsShortAndConsumesNothing) {
    RecipeBook book;
    const Recipe* r = findResult(book, ItemId::FlintAndSteel);
    ASSERT_NE(r, nullptr);
    Inventory inv;
    inv.setSlot(3, {ItemId::IronIngot, 1}); // no flint

    const CraftResult out = book.quickCraft(*r, inv);
    EXPECT_FALSE(out.ok);
    EXPECT_EQ(inv.slot(3).item, ItemId::IronIngot);
    EXPECT_EQ(inv.slot(3).count, 1u);
}

// --- Items and display names ---------------------------------------------

TEST(RecipeBook, NewSwordsStackToOnlyOne) {
    EXPECT_EQ(maxStackSize(ItemId::WoodenSword), 1u);
    EXPECT_EQ(maxStackSize(ItemId::StoneSword), 1u);
    EXPECT_EQ(maxStackSize(ItemId::IronSword), 1u);
    EXPECT_EQ(maxStackSize(ItemId::NetheriteSword), 1u);
    EXPECT_EQ(maxStackSize(ItemId::DiamondSword), 1u);
    EXPECT_EQ(maxStackSize(ItemId::WoodPlanks), 64u);
}

TEST(RecipeBook, NewItemsHaveDisplayNames) {
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::WoodenSword), "Wooden Sword");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::StoneSword), "Stone Sword");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::IronSword), "Iron Sword");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::NetheriteSword), "Netherite Sword");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::CraftingTable), "Crafting Table");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::Anvil), "Anvil");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::WoodPlanks), "Wood Planks");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::Stick), "Stick");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::IronIngot), "Iron Ingot");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::GoldIngot), "Gold Ingot");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::NetheriteIngot), "Netherite Ingot");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::String), "String");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::Gunpowder), "Gunpowder");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::Sand), "Sand");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::Flint), "Flint");
    EXPECT_STREQ(HudEngine::getItemDisplayName(ItemId::Apple), "Apple");
}
