#include <gtest/gtest.h>

#include "mc/er_region_table.hpp"
#include "mc/zone_detector.hpp"

using namespace mc;

TEST(ZoneDetectorTest, DecodesBlockIdFields) {
    const WorldBlockId b = decodeBlockId(0x3C2A2400u); // area 60, grid 42/36, LOD 0
    EXPECT_TRUE(b.valid);
    EXPECT_EQ(b.raw, 0x3C2A2400u);
    EXPECT_EQ(b.area, 60);
    EXPECT_EQ(b.grid_x, 42);
    EXPECT_EQ(b.grid_z, 36);
    EXPECT_EQ(b.lod, 0);
}

TEST(ZoneDetectorTest, InvalidBlockIdsStayInvalid) {
    EXPECT_FALSE(decodeBlockId(0).valid);
    EXPECT_FALSE(decodeBlockId(0xFFFFFFFFu).valid);
}

TEST(ZoneDetectorTest, MaterialRuleWinsOverRegionAndAreaRules) {
    static constexpr ZoneRule material[] = {{1001, LootZone::Volcano}};
    static constexpr ZoneRule area[] = {{60, LootZone::Cave}};
    const ZoneRules rules{.material_rules = material, .region_rules = er_region::kTable,
                          .area_rules = area, .fallback = LootZone::Cave};

    EXPECT_EQ(classifyZone(decodeBlockId(0x3C2A2400u), 1001, rules), LootZone::Volcano);
}

TEST(ZoneDetectorTest, RegionTableWinsOverAreaRule) {
    static constexpr ZoneRule area[] = {{60, LootZone::StoneSurface}};
    const ZoneRules rules{.region_rules = er_region::kTable, .area_rules = area,
                          .fallback = LootZone::Cave};

    EXPECT_EQ(classifyZone(decodeBlockId(0x3C2A2400u), 0, rules), LootZone::Grassland); // Limgrave tile
}

TEST(ZoneDetectorTest, RegionTableMapsKnownTiles) {
    const ZoneRules rules{.region_rules = er_region::kTable, .fallback = LootZone::Grassland};

    EXPECT_EQ(classifyZone(decodeBlockId(0x3C2A2400u), 0, rules), LootZone::Grassland);    // Limgrave
    EXPECT_EQ(classifyZone(decodeBlockId(0x10000000u), 0, rules), LootZone::Volcano);      // Volcano Manor
    EXPECT_EQ(classifyZone(decodeBlockId(0x20080000u), 0, rules), LootZone::CrystalCave);  // Sellia Crystal Tunnel
    EXPECT_EQ(classifyZone(decodeBlockId(0x0C030000u), 0, rules), LootZone::Deeproot);     // Deeproot Depths
    EXPECT_EQ(classifyZone(decodeBlockId(0x0A000000u), 0, rules), LootZone::StoneSurface); // Stormveil Castle
    EXPECT_EQ(classifyZone(decodeBlockId(0x1E000000u), 0, rules), LootZone::Cave);         // Tombsward Catacombs
    EXPECT_EQ(classifyZone(decodeBlockId(0x3C2A2401u), 0, rules), LootZone::Grassland);    // LOD 1 falls back to LOD 0
}

TEST(ZoneDetectorTest, AreaRuleAppliesWhenTheTileIsUnknown) {
    static constexpr ZoneRule area[] = {{60, LootZone::Grassland}, {61, LootZone::Cave}};
    const ZoneRules rules{.area_rules = area, .fallback = LootZone::StoneSurface};

    EXPECT_EQ(classifyZone(decodeBlockId(0x3C000000u), 0, rules), LootZone::Grassland);
    EXPECT_EQ(classifyZone(decodeBlockId(0x3D000000u), 0, rules), LootZone::Cave);
}

TEST(ZoneDetectorTest, UnknownAreaAndMaterialUseTheFallback) {
    static constexpr ZoneRule area[] = {{60, LootZone::Grassland}};
    const ZoneRules rules{.area_rules = area, .fallback = LootZone::Deeproot};

    EXPECT_EQ(classifyZone(decodeBlockId(0x0A000000u), 0, rules), LootZone::Deeproot);
    EXPECT_EQ(classifyZone(decodeBlockId(0), 0, rules), LootZone::Deeproot);
}
