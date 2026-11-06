#include <gtest/gtest.h>

#include "mc/block_entity.hpp"
#include "mc/blocks.hpp"
#include "mc/inventory_layout.hpp"

#include <variant>

using namespace mc;

namespace {

ItemStack dirt(uint32_t count) { return {ItemId::BlockDirt, count}; }

// A row of `count` blocks along +X starting at the origin, for the piston chain tests.
BlockGrid rowAlongX(int count, BlockId id) {
    BlockGrid grid;
    for (int i = 0; i < count; ++i) grid.place({i, 0, 0}, id);
    return grid;
}

} // namespace

// --- Container -------------------------------------------------------------------------------

TEST(BlockEntityContainer, HasTwentySevenSlotsAndStacksToSixtyFour) {
    Container chest;
    EXPECT_EQ(Container::kSlots, 27);
    EXPECT_EQ(chest.emptySlots(), 27);
    EXPECT_TRUE(chest.insert(dirt(100)).empty());
    EXPECT_EQ(chest.get(0).item, ItemId::BlockDirt);
    EXPECT_EQ(chest.get(0).count, 64u);
    EXPECT_EQ(chest.get(1).count, 36u);
    EXPECT_EQ(chest.emptySlots(), 25);
    EXPECT_EQ(chest.count(ItemId::BlockDirt), 100u);
}

TEST(BlockEntityContainer, FillsPartialStacksBeforeOpeningANewSlot) {
    Container chest;
    chest.set(3, dirt(60));
    EXPECT_TRUE(chest.insert(dirt(10)).empty());
    EXPECT_EQ(chest.get(3).count, 64u);
    EXPECT_EQ(chest.get(0).count, 6u);
    EXPECT_EQ(chest.emptySlots(), 25);
}

TEST(BlockEntityContainer, ReturnsTheRemainderWhenItRunsOutOfRoom) {
    Container chest;
    for (int i = 0; i < Container::kSlots; ++i) chest.set(i, dirt(64));
    EXPECT_EQ(chest.emptySlots(), 0);
    const ItemStack leftover = chest.insert(dirt(30));
    EXPECT_EQ(leftover.item, ItemId::BlockDirt);
    EXPECT_EQ(leftover.count, 30u);
    EXPECT_EQ(chest.count(ItemId::BlockDirt), 27u * 64u);
}

TEST(BlockEntityContainer, RefusesATwentyEighthDistinctStack) {
    Container chest;
    const ItemId items[] = {
        ItemId::BlockDirt,        ItemId::BlockStone,     ItemId::BlockTnt,      ItemId::BlockWood,
        ItemId::BlockGlass,       ItemId::Torch,          ItemId::Stick,         ItemId::Coal,
        ItemId::RawIron,          ItemId::RawGold,        ItemId::Redstone,      ItemId::LapisLazuli,
        ItemId::AmethystShard,    ItemId::Diamond,        ItemId::Emerald,       ItemId::NetheriteScrap,
        ItemId::BlockCobblestone, ItemId::BlockObsidian,  ItemId::WoodPlanks,    ItemId::Sand,
        ItemId::Flint,            ItemId::String,         ItemId::Gunpowder,     ItemId::Apple,
        ItemId::BlockGravel,      ItemId::BlockBricks,    ItemId::BlockWhiteWool, ItemId::BlockLeaves};
    for (int i = 0; i < Container::kSlots; ++i) EXPECT_TRUE(chest.insert({items[i], 1}).empty());
    EXPECT_EQ(chest.emptySlots(), 0);
    const ItemStack leftover = chest.insert({ItemId::BlockStoneBricks, 5});
    EXPECT_EQ(leftover.item, ItemId::BlockStoneBricks);
    EXPECT_EQ(leftover.count, 5u);
}

TEST(BlockEntityContainer, SplitsAStackAcrossSlotsWhenTheItemStacksToSixteen) {
    Container chest;
    EXPECT_TRUE(chest.insert({ItemId::EnderPearl, 40}).empty());
    EXPECT_EQ(chest.get(0).count, 16u);
    EXPECT_EQ(chest.get(1).count, 16u);
    EXPECT_EQ(chest.get(2).count, 8u);
    EXPECT_EQ(chest.count(ItemId::EnderPearl), 40u);
}

TEST(BlockEntityContainer, RemoveReturnsTheRightItemsAndClearsTheSlotAtZero) {
    Container chest;
    chest.set(4, {ItemId::Diamond, 10});
    const ItemStack part = chest.remove(4, 4);
    EXPECT_EQ(part.item, ItemId::Diamond);
    EXPECT_EQ(part.count, 4u);
    EXPECT_EQ(chest.get(4).count, 6u);

    const ItemStack rest = chest.remove(4, 99);
    EXPECT_EQ(rest.item, ItemId::Diamond);
    EXPECT_EQ(rest.count, 6u);
    EXPECT_TRUE(chest.get(4).empty());
    EXPECT_EQ(chest.get(4).item, ItemId::None);
    EXPECT_TRUE(chest.remove(4, 1).empty()); // already empty
    EXPECT_TRUE(chest.remove(Container::kSlots, 1).empty());
    EXPECT_TRUE(chest.remove(-1, 1).empty());
}

TEST(BlockEntityContainer, SetRejectsOutOfRangeAndClampsToTheStackSize) {
    Container chest;
    EXPECT_FALSE(chest.set(-1, dirt(1)));
    EXPECT_FALSE(chest.set(Container::kSlots, dirt(1)));
    EXPECT_TRUE(chest.set(0, dirt(99)));
    EXPECT_EQ(chest.get(0).count, 64u);
    EXPECT_TRUE(chest.set(0, {}));
    EXPECT_TRUE(chest.get(0).empty());
}

// --- Bed -------------------------------------------------------------------------------------

TEST(BlockEntityBed, HalvesResolveToEachOtherAndRejectAThirdCell) {
    const Cell head{4, 64, 4};
    const Cell foot{4, 64, 5};
    Bed bed(DyeColor::Red, BedHalf::Head, head, foot);
    EXPECT_EQ(bed.color(), DyeColor::Red);
    EXPECT_EQ(bed.half(), BedHalf::Head);
    EXPECT_EQ(bed.head(), head);
    EXPECT_EQ(bed.foot(), foot);

    ASSERT_TRUE(bed.otherHalf(head).has_value());
    EXPECT_EQ(*bed.otherHalf(head), foot);
    ASSERT_TRUE(bed.otherHalf(foot).has_value());
    EXPECT_EQ(*bed.otherHalf(foot), head);
    EXPECT_FALSE(bed.otherHalf({4, 64, 6}).has_value());
    EXPECT_FALSE(bed.otherHalf({0, 0, 0}).has_value());
}

TEST(BlockEntityBed, OccupancyTracksSleepAndCellsMustDiffer) {
    Bed bed;
    EXPECT_FALSE(bed.occupied());
    bed.setOccupied(true);
    EXPECT_TRUE(bed.occupied());
    bed.setOccupied(false);
    EXPECT_FALSE(bed.occupied());

    EXPECT_TRUE(bed.setCells({1, 2, 3}, {1, 2, 4}));
    EXPECT_FALSE(bed.setCells({1, 2, 3}, {1, 2, 3})); // head and foot cannot share a cell
    EXPECT_EQ(bed.head(), (Cell{1, 2, 3}));
    EXPECT_EQ(bed.foot(), (Cell{1, 2, 4}));
}

// --- Banner ----------------------------------------------------------------------------------

TEST(BlockEntityBanner, AcceptsSixPatternsAndRejectsTheSeventh) {
    Banner banner(DyeColor::White);
    EXPECT_EQ(banner.baseColor(), DyeColor::White);
    EXPECT_EQ(banner.patternCount(), 0);
    EXPECT_TRUE(banner.patterns().empty());

    EXPECT_TRUE(banner.addPattern(BannerPatternKind::StripeMiddle, DyeColor::Red));
    EXPECT_TRUE(banner.addPattern(BannerPatternKind::Cross, DyeColor::Blue));
    EXPECT_TRUE(banner.addPattern(BannerPatternKind::Border, DyeColor::Black));
    EXPECT_TRUE(banner.addPattern(BannerPatternKind::Creeper, DyeColor::Green));
    EXPECT_TRUE(banner.addPattern(BannerPatternKind::Rhombus, DyeColor::Yellow));
    EXPECT_TRUE(banner.addPattern(BannerPatternKind::Skull, DyeColor::Gray));
    EXPECT_EQ(banner.patternCount(), 6);

    EXPECT_FALSE(banner.addPattern(BannerPatternKind::Flower, DyeColor::Pink));
    EXPECT_EQ(banner.patternCount(), 6);
    EXPECT_EQ(banner.patterns().size(), 6u);
    EXPECT_EQ(banner.patterns()[0].kind, BannerPatternKind::StripeMiddle);
    EXPECT_EQ(banner.patterns()[0].color, DyeColor::Red);
    EXPECT_EQ(banner.patterns()[5].kind, BannerPatternKind::Skull);
    EXPECT_EQ(banner.patterns()[5].color, DyeColor::Gray);

    banner.clearPatterns();
    EXPECT_EQ(banner.patternCount(), 0);
    EXPECT_TRUE(banner.addPattern(BannerPatternKind::Flower, DyeColor::Pink));
}

// --- Skull -----------------------------------------------------------------------------------

TEST(BlockEntitySkull, OnlyPlayerSkullsCarryAnOwner) {
    Skull skull(SkullKind::Player);
    EXPECT_EQ(skull.kind(), SkullKind::Player);
    EXPECT_TRUE(skull.owner().empty());
    EXPECT_TRUE(skull.setOwner("Steve"));
    EXPECT_EQ(skull.owner(), "Steve");
    EXPECT_FALSE(skull.setOwner(""));
    EXPECT_EQ(skull.owner(), "Steve");
    skull.clearOwner();
    EXPECT_TRUE(skull.owner().empty());

    Skull creeper(SkullKind::Creeper);
    EXPECT_FALSE(creeper.setOwner("Steve"));
    EXPECT_TRUE(creeper.owner().empty());

    creeper.setKind(SkullKind::Player);
    EXPECT_TRUE(creeper.setOwner("Alex"));
    creeper.setKind(SkullKind::Zombie);
    EXPECT_TRUE(creeper.owner().empty()); // dropping the Player kind drops the owner
}

// --- Piston ----------------------------------------------------------------------------------

TEST(BlockEntityPiston, DirectionAndStateAreIndependent) {
    Piston piston(Direction::Up, true);
    EXPECT_EQ(piston.direction(), Direction::Up);
    EXPECT_TRUE(piston.sticky());
    EXPECT_FALSE(piston.extended());
    piston.setExtended(true);
    EXPECT_TRUE(piston.extended());
    piston.setSticky(false);
    EXPECT_FALSE(piston.sticky());
    piston.setDirection(Direction::West);
    EXPECT_EQ(piston.direction(), Direction::West);
    EXPECT_EQ(piston.headCell({10, 0, 0}), (Cell{9, 0, 0}));
}

TEST(BlockEntityPiston, DirectionOffsetsCoverTheSixFaces) {
    EXPECT_EQ(directionOffset(Direction::Down), (Cell{0, -1, 0}));
    EXPECT_EQ(directionOffset(Direction::Up), (Cell{0, 1, 0}));
    EXPECT_EQ(directionOffset(Direction::North), (Cell{0, 0, -1}));
    EXPECT_EQ(directionOffset(Direction::South), (Cell{0, 0, 1}));
    EXPECT_EQ(directionOffset(Direction::West), (Cell{-1, 0, 0}));
    EXPECT_EQ(directionOffset(Direction::East), (Cell{1, 0, 0}));
}

TEST(BlockEntityPiston, ImmovableSetMatchesVanilla) {
    EXPECT_TRUE(PistonRules::isImmovable(BlockId::Obsidian));
    EXPECT_TRUE(PistonRules::isImmovable(BlockId::Anvil));
    EXPECT_FALSE(PistonRules::isImmovable(BlockId::Stone));
    EXPECT_FALSE(PistonRules::isImmovable(BlockId::Dirt));
    EXPECT_FALSE(PistonRules::isImmovable(BlockId::Air)); // air passes through
    EXPECT_TRUE(PistonRules::isPassThrough(BlockId::Air));
    EXPECT_FALSE(PistonRules::isPassThrough(BlockId::Stone));
}

TEST(BlockEntityPiston, PushesUpToTwelveBlocks) {
    const BlockGrid grid = rowAlongX(12, BlockId::Stone);
    const PistonMove plan = PistonRules::planMove(grid, {-1, 0, 0}, Direction::East, false);
    EXPECT_TRUE(plan.possible);
    EXPECT_EQ(plan.moved.size(), 12u);
    EXPECT_EQ(plan.moved.front(), (Cell{0, 0, 0}));
    EXPECT_EQ(plan.moved.back(), (Cell{11, 0, 0}));
    EXPECT_FALSE(plan.pulled.has_value());
}

TEST(BlockEntityPiston, RefusesAChainOfThirteen) {
    const BlockGrid grid = rowAlongX(13, BlockId::Stone);
    const PistonMove plan = PistonRules::planMove(grid, {-1, 0, 0}, Direction::East, false);
    EXPECT_FALSE(plan.possible);
}

TEST(BlockEntityPiston, RefusesAChainContainingObsidian) {
    // BlockGrid::place refuses to overwrite, so build the mixed rows cell by cell.
    BlockGrid grid = rowAlongX(2, BlockId::Stone);
    grid.place({2, 0, 0}, BlockId::Obsidian);
    grid.place({3, 0, 0}, BlockId::Stone);
    const PistonMove plan = PistonRules::planMove(grid, {-1, 0, 0}, Direction::East, false);
    EXPECT_FALSE(plan.possible);
    EXPECT_EQ(plan.moved.size(), 2u); // the two stone blocks before the obsidian

    BlockGrid anvil;
    anvil.place({0, 0, 0}, BlockId::Anvil);
    anvil.place({1, 0, 0}, BlockId::Stone);
    EXPECT_FALSE(PistonRules::planMove(anvil, {-1, 0, 0}, Direction::East, false).possible);
}

TEST(BlockEntityPiston, ExtendingIntoAirMovesNothing) {
    const BlockGrid grid;
    const PistonMove plan = PistonRules::planMove(grid, {0, 0, 0}, Direction::Up, false);
    EXPECT_TRUE(plan.possible);
    EXPECT_TRUE(plan.moved.empty());
}

TEST(BlockEntityPiston, StickyRetractionPullsExactlyOneBlock) {
    const BlockGrid grid = rowAlongX(3, BlockId::Stone);
    const PistonMove plan = PistonRules::planMove(grid, {-1, 0, 0}, Direction::East, true, PistonAction::Retract);
    EXPECT_TRUE(plan.possible);
    ASSERT_TRUE(plan.pulled.has_value());
    EXPECT_EQ(*plan.pulled, (Cell{0, 0, 0})); // only the block against the head
    EXPECT_TRUE(plan.moved.empty());

    const PistonMove plain = PistonRules::planMove(grid, {-1, 0, 0}, Direction::East, false, PistonAction::Retract);
    EXPECT_TRUE(plain.possible);
    EXPECT_FALSE(plain.pulled.has_value());
}

TEST(BlockEntityPiston, StickyRetractionPullsNothingFromAirOrObsidian) {
    const BlockGrid empty;
    EXPECT_FALSE(PistonRules::planMove(empty, {0, 0, 0}, Direction::Up, true, PistonAction::Retract).pulled.has_value());

    BlockGrid blocked;
    blocked.place({0, 1, 0}, BlockId::Obsidian);
    EXPECT_FALSE(PistonRules::planMove(blocked, {0, 0, 0}, Direction::Up, true, PistonAction::Retract).pulled.has_value());
}

// --- Registry --------------------------------------------------------------------------------

TEST(BlockEntityRegistry, CreateGetRemoveAndVersionBumps) {
    BlockEntityRegistry registry;
    EXPECT_EQ(registry.version(), 0u);
    EXPECT_TRUE(registry.all().empty());
    EXPECT_FALSE(registry.contains({1, 2, 3}));
    EXPECT_EQ(registry.get({1, 2, 3}), nullptr);

    const Cell cell{1, 2, 3};
    BlockEntity* chest = registry.create(cell, BlockEntityKind::Chest);
    ASSERT_NE(chest, nullptr);
    EXPECT_EQ(chest->kind, BlockEntityKind::Chest);
    EXPECT_TRUE(std::holds_alternative<Container>(chest->payload));
    EXPECT_TRUE(registry.contains(cell));
    EXPECT_EQ(registry.version(), 1u);
    EXPECT_EQ(registry.all().size(), 1u);

    // A cell holds at most one block entity.
    EXPECT_EQ(registry.create(cell, BlockEntityKind::Banner), nullptr);
    EXPECT_EQ(registry.version(), 1u);

    EXPECT_TRUE(registry.remove(cell));
    EXPECT_EQ(registry.version(), 2u);
    EXPECT_FALSE(registry.remove(cell));
    EXPECT_EQ(registry.version(), 2u);
    EXPECT_FALSE(registry.contains(cell));
}

TEST(BlockEntityRegistry, CreateBuildsThePayloadForEachKind) {
    BlockEntityRegistry registry;
    const struct {
        BlockEntityKind kind;
        size_t payload;
    } cases[] = {{BlockEntityKind::Chest, 0},      {BlockEntityKind::TrappedChest, 0},
                 {BlockEntityKind::ShulkerBox, 0}, {BlockEntityKind::Bed, 1},
                 {BlockEntityKind::Banner, 2},     {BlockEntityKind::Skull, 3},
                 {BlockEntityKind::Piston, 4}};
    int x = 0;
    for (const auto& c : cases) {
        BlockEntity* e = registry.create({x++, 0, 0}, c.kind);
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(e->kind, c.kind);
        EXPECT_EQ(e->payload.index(), c.payload);
    }
    EXPECT_EQ(registry.all().size(), 7u);
    EXPECT_EQ(registry.version(), 7u);
}

TEST(BlockEntityRegistry, PayloadsAreEditableInPlaceAndTouchBumpsTheVersion) {
    BlockEntityRegistry registry;
    const Cell cell{0, 64, 0};
    registry.create(cell, BlockEntityKind::Chest);
    Container* chest = registry.getAs<Container>(cell);
    ASSERT_NE(chest, nullptr);
    EXPECT_TRUE(chest->insert(dirt(64)).empty());
    EXPECT_EQ(registry.getAs<Container>(cell)->get(0).count, 64u);
    EXPECT_EQ(registry.version(), 1u); // payload edits alone do not bump

    registry.touch(cell);
    EXPECT_EQ(registry.version(), 2u);
    registry.touch({9, 9, 9}); // unknown cell: no bump
    EXPECT_EQ(registry.version(), 2u);
    EXPECT_EQ(registry.getAs<Banner>(cell), nullptr); // wrong type
}

TEST(BlockEntityRegistry, ClearRemovesEverythingAndBumpsOnce) {
    BlockEntityRegistry registry;
    registry.create({0, 0, 0}, BlockEntityKind::Chest);
    registry.create({0, 0, 1}, BlockEntityKind::Bed);
    EXPECT_EQ(registry.version(), 2u);

    registry.clear();
    EXPECT_TRUE(registry.all().empty());
    EXPECT_EQ(registry.version(), 3u);
    registry.clear(); // already empty: no bump
    EXPECT_EQ(registry.version(), 3u);
}

// --- Container clicks (the chest screen) ------------------------------------------------------

TEST(ContainerClickTest, LeftClickPicksUpPlacesMergesAndSwaps) {
    Container chest;
    Inventory player;

    chest.set(0, dirt(10));
    containerClick(chest, 0, Button::Left, false, player);
    EXPECT_TRUE(chest.get(0).empty());
    EXPECT_EQ(player.cursor().count, 10u);

    containerClick(chest, 1, Button::Left, false, player);
    EXPECT_TRUE(player.cursor().empty());
    EXPECT_EQ(chest.get(1).count, 10u);

    chest.set(2, dirt(60));
    player.setCursor(dirt(10));
    containerClick(chest, 2, Button::Left, false, player);
    EXPECT_EQ(chest.get(2).count, 64u);
    EXPECT_EQ(player.cursor().count, 6u);

    chest.set(3, {ItemId::BlockStone, 5});
    player.setCursor({ItemId::BlockWood, 2});
    containerClick(chest, 3, Button::Left, false, player);
    EXPECT_EQ(chest.get(3).item, ItemId::BlockWood);
    EXPECT_EQ(player.cursor().item, ItemId::BlockStone);
}

TEST(ContainerClickTest, RightClickSplitsAndPlacesOne) {
    Container chest;
    Inventory player;

    chest.set(0, dirt(9));
    containerClick(chest, 0, Button::Right, false, player);
    EXPECT_EQ(player.cursor().count, 5u); // vanilla takes the larger half
    EXPECT_EQ(chest.get(0).count, 4u);

    containerClick(chest, 1, Button::Right, false, player);
    EXPECT_EQ(chest.get(1).count, 1u);
    EXPECT_EQ(player.cursor().count, 4u);
}

TEST(ContainerClickTest, ShiftMovesChestIntoInventory) {
    Container chest;
    Inventory player;
    chest.set(0, dirt(20));
    containerClick(chest, 0, Button::Left, true, player);
    EXPECT_TRUE(chest.get(0).empty());
    EXPECT_EQ(player.slot(0).count, 20u);
}

TEST(ContainerClickTest, ShiftKeepsTheLeftoverInTheChest) {
    Container chest;
    Inventory player;
    for (int i = 0; i < Inventory::kSlots; ++i) player.setSlot(i, {ItemId::BlockStone, 64});
    chest.set(0, dirt(10));
    containerClick(chest, 0, Button::Left, true, player);
    EXPECT_EQ(chest.get(0).count, 10u); // nothing fit
}

TEST(ContainerClickTest, ShiftFromPlayerMovesIntoTheChest) {
    Container chest;
    Inventory player;
    player.setSlot(3, dirt(12));
    playerShiftClickIntoContainer(chest, player, 3);
    EXPECT_TRUE(player.slot(3).empty());
    EXPECT_EQ(chest.get(0).count, 12u);
}

TEST(ChestLayoutTest, ChestSlotsAndInventoryOnly) {
    const InventoryLayout lay(1920.f, 1080.f, false, true);
    const HudRect chest0 = lay.chestSlot(0);
    const SlotRef ref0 = lay.hitTest(chest0.x + 1.f, chest0.y + 1.f);
    EXPECT_EQ(ref0.kind, SlotRef::Kind::Chest);
    EXPECT_EQ(ref0.index, 0);

    const HudRect chest26 = lay.chestSlot(26);
    const SlotRef ref26 = lay.hitTest(chest26.x + 1.f, chest26.y + 1.f);
    EXPECT_EQ(ref26.kind, SlotRef::Kind::Chest);
    EXPECT_EQ(ref26.index, 26);

    const HudRect inv = lay.invSlot(0);
    EXPECT_EQ(lay.hitTest(inv.x + 1.f, inv.y + 1.f).kind, SlotRef::Kind::Inventory);

    // Armor, offhand and crafting are not part of the chest screen.
    const HudRect armor = lay.armorSlot(0);
    EXPECT_NE(lay.hitTest(armor.x + 1.f, armor.y + 1.f).kind, SlotRef::Kind::Armor);
    EXPECT_NE(lay.hitTest(armor.x + 1.f, armor.y + 1.f).kind, SlotRef::Kind::Offhand);
}
