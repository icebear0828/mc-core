#include <gtest/gtest.h>

#include "mc/loot_table.hpp"

#include <array>
#include <cstdint>

using namespace mc;

namespace {

constexpr LootZone kZones[] = {LootZone::Grassland, LootZone::StoneSurface, LootZone::Cave,
                               LootZone::CrystalCave, LootZone::Volcano, LootZone::Deeproot};

struct Sample {
    int iron{0};
    int diamond{0};
    int coal{0};
    int cobblestone{0};
    int other_high_tier{0};
    int total{0};
};

Sample sampleBase(LootZone zone, ToolTier tier, int rolls, uint64_t seed = 0x1234567890ABCDEFull) {
    LootTable table;
    uint64_t rng = seed;
    Sample s;
    for (int i = 0; i < rolls; ++i) {
        const LootRoll r = table.rollBase(zone, tier, rng);
        ++s.total;
        if (r.item == ItemId::RawIron) ++s.iron;
        if (r.item == ItemId::Diamond) ++s.diamond;
        if (r.item == ItemId::Coal) ++s.coal;
        if (r.item == ItemId::BlockCobblestone) ++s.cobblestone;
        if (r.item == ItemId::RawGold || r.item == ItemId::Redstone || r.item == ItemId::AmethystShard ||
            r.item == ItemId::Emerald || r.item == ItemId::NetheriteScrap) {
            ++s.other_high_tier;
        }
    }
    return s;
}

} // namespace

TEST(LootTableTest, DefaultTableZoneWeightSumsMatchIssue81) {
    const LootTable table;
    EXPECT_FLOAT_EQ(table.zoneWeightSum(LootZone::Grassland), 100.0f);
    EXPECT_FLOAT_EQ(table.zoneWeightSum(LootZone::StoneSurface), 100.0f);
    EXPECT_FLOAT_EQ(table.zoneWeightSum(LootZone::Cave), 100.0f);
    EXPECT_FLOAT_EQ(table.zoneWeightSum(LootZone::CrystalCave), 100.0f);
    // Issue #81 lists the volcano column as 80.5 in total (the missing 19.5 is not specified).
    // roll() normalises by the actual sum, so the column is usable as written.
    EXPECT_FLOAT_EQ(table.zoneWeightSum(LootZone::Volcano), 80.5f);
    EXPECT_FLOAT_EQ(table.zoneWeightSum(LootZone::Deeproot), 100.0f);
    EXPECT_FALSE(table.zonesSumTo100());
}

TEST(LootTableTest, CaveIronRateMatchesAcceptanceWindow) {
    const Sample s = sampleBase(LootZone::Cave, ToolTier::Diamond, 100000);
    const float iron_rate = static_cast<float>(s.iron) / static_cast<float>(s.total);
    const float diamond_rate = static_cast<float>(s.diamond) / static_cast<float>(s.total);
    EXPECT_GE(iron_rate, 0.19f);
    EXPECT_LE(iron_rate, 0.21f);
    EXPECT_NEAR(diamond_rate, 0.015f, 0.002f);
}

TEST(LootTableTest, HandTierOnlyYieldsCobblestoneAndCoalInCave) {
    const Sample s = sampleBase(LootZone::Cave, ToolTier::Hand, 40000);
    EXPECT_EQ(s.iron, 0);
    EXPECT_EQ(s.diamond, 0);
    EXPECT_EQ(s.other_high_tier, 0);
    const float coal_rate = static_cast<float>(s.coal) / static_cast<float>(s.total);
    EXPECT_NEAR(coal_rate, 0.18f, 0.01f);
    // Everything above hand tier is downgraded to rubble, not rerolled.
    EXPECT_NEAR(static_cast<float>(s.cobblestone) / static_cast<float>(s.total), 0.82f, 0.01f);
}

TEST(LootTableTest, StoneTierUnlocksCopperIronAndLapisOnly) {
    const Sample s = sampleBase(LootZone::Cave, ToolTier::Stone, 40000);
    EXPECT_GT(s.iron, 0);
    EXPECT_EQ(s.other_high_tier, 0); // gold, redstone, amethyst, emerald and netherite need iron
    EXPECT_EQ(s.diamond, 0);
}

TEST(LootTableTest, SeededRollsAreReproducible) {
    LootTable a;
    LootTable b;
    uint64_t ra = 42;
    uint64_t rb = 42;
    for (int i = 0; i < 200; ++i) {
        EXPECT_EQ(a.rollBase(LootZone::Deeproot, ToolTier::Netherite, ra).item,
                  b.rollBase(LootZone::Deeproot, ToolTier::Netherite, rb).item);
    }
}

TEST(LootTableTest, PityCounterRaisesIronAfterFiveMissesAndResetsOnIron) {
    // Dirt only until the pity bonus gives the zero-weight iron entry a chance.
    static constexpr std::array<LootEntry, 2> kPityTable{{
        {ItemId::BlockDirt, {0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f}, ToolTier::Hand},
        {ItemId::RawIron, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, ToolTier::Stone},
    }};
    LootTable table(kPityTable);
    uint64_t rng = 0xC0FFEEull;

    for (int i = 0; i < LootTable::kPityThreshold; ++i) {
        const LootRoll r = table.roll(LootZone::Cave, ToolTier::Diamond, rng, /*allow_combo=*/false);
        EXPECT_EQ(r.item, ItemId::BlockDirt);
        EXPECT_FALSE(r.pity_triggered);
    }
    EXPECT_EQ(table.pityMisses(), LootTable::kPityThreshold);
    EXPECT_FLOAT_EQ(table.pityBonus(), LootTable::kPityStep);

    bool got_iron = false;
    for (int i = 0; i < 200; ++i) {
        const LootRoll r = table.roll(LootZone::Cave, ToolTier::Diamond, rng, /*allow_combo=*/false);
        if (r.item == ItemId::RawIron) {
            EXPECT_TRUE(r.pity_triggered);
            got_iron = true;
            break;
        }
    }
    EXPECT_TRUE(got_iron);
    EXPECT_EQ(table.pityMisses(), 0);
    EXPECT_FLOAT_EQ(table.pityBonus(), 0.0f);
}

TEST(LootTableTest, SurfaceRollsResetPityAndCombo) {
    LootTable table;
    uint64_t rng = 7;
    table.roll(LootZone::Cave, ToolTier::Diamond, rng, /*allow_combo=*/false);
    table.roll(LootZone::Cave, ToolTier::Diamond, rng, /*allow_combo=*/false);
    EXPECT_EQ(table.combo(), 2);
    table.roll(LootZone::Grassland, ToolTier::Diamond, rng);
    EXPECT_EQ(table.combo(), 0);
    EXPECT_EQ(table.pityMisses(), 0);
}

TEST(LootTableTest, FourthUndergroundDigTriggersVeinCombo) {
    LootTable table;
    uint64_t rng = 0xABCDEFull;
    for (int cycle = 0; cycle < 20; ++cycle) {
        LootRoll fourth;
        for (int i = 0; i < LootTable::kComboLength; ++i) {
            fourth = table.roll(LootZone::Cave, ToolTier::Diamond, rng);
        }
        EXPECT_TRUE(fourth.combo_triggered);
        EXPECT_TRUE(isMineral(fourth.item)); // the combo roll only considers minerals
        EXPECT_EQ(table.combo(), 0);
        if (fourth.bonus != ItemId::None) {
            EXPECT_TRUE(fourth.bonus == ItemId::Coal || fourth.bonus == ItemId::Redstone);
        }
    }
}

TEST(LootTableTest, ComboBonusNeverGivesRedstoneBelowIronTier) {
    LootTable table;
    uint64_t rng = 99;
    for (int cycle = 0; cycle < 40; ++cycle) {
        LootRoll fourth;
        for (int i = 0; i < LootTable::kComboLength; ++i) {
            fourth = table.roll(LootZone::Cave, ToolTier::Hand, rng);
        }
        EXPECT_TRUE(fourth.combo_triggered);
        // A hand cannot harvest the ore the combo picked, so issue #81's tier gate turns it into rubble.
        EXPECT_TRUE(isMineral(fourth.item) || fourth.item == ItemId::BlockCobblestone);
        EXPECT_NE(fourth.bonus, ItemId::Redstone);
    }
}

TEST(LootTableTest, PickaxeTiersAndZoneClassification) {
    EXPECT_TRUE(isPickaxe(ItemId::DiamondPickaxe));
    EXPECT_TRUE(isPickaxe(ItemId::NetheritePickaxe));
    EXPECT_FALSE(isPickaxe(ItemId::DiamondSword));
    EXPECT_FALSE(isPickaxe(ItemId::None));
    EXPECT_EQ(toolTier(ItemId::None), ToolTier::Hand);
    EXPECT_EQ(toolTier(ItemId::WoodenPickaxe), ToolTier::Wood);
    EXPECT_EQ(toolTier(ItemId::StonePickaxe), ToolTier::Stone);
    EXPECT_EQ(toolTier(ItemId::IronPickaxe), ToolTier::Iron);
    EXPECT_EQ(toolTier(ItemId::GoldenPickaxe), ToolTier::Gold);
    EXPECT_EQ(toolTier(ItemId::DiamondPickaxe), ToolTier::Diamond);
    EXPECT_EQ(toolTier(ItemId::NetheritePickaxe), ToolTier::Netherite);

    EXPECT_FALSE(isUnderground(LootZone::Grassland));
    EXPECT_FALSE(isUnderground(LootZone::StoneSurface));
    for (LootZone zone : {LootZone::Cave, LootZone::CrystalCave, LootZone::Volcano, LootZone::Deeproot}) {
        EXPECT_TRUE(isUnderground(zone));
    }
}
