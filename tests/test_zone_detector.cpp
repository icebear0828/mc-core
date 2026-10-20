#include <gtest/gtest.h>

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

TEST(ZoneDetectorTest, MaterialRuleWinsOverAreaRule) {
    static constexpr ZoneRule material[] = {{1001, LootZone::StoneSurface}};
    static constexpr ZoneRule area[] = {{60, LootZone::Grassland}};
    const ZoneRules rules{material, area, LootZone::Cave};

    EXPECT_EQ(classifyZone(decodeBlockId(0x3C2A2400u), 1001, rules), LootZone::StoneSurface);
}

TEST(ZoneDetectorTest, AreaRuleAppliesWhenMaterialIsUnknown) {
    static constexpr ZoneRule area[] = {{60, LootZone::Grassland}, {61, LootZone::Cave}};
    const ZoneRules rules{{}, area, LootZone::StoneSurface};

    EXPECT_EQ(classifyZone(decodeBlockId(0x3C2A2400u), 0, rules), LootZone::Grassland);
    EXPECT_EQ(classifyZone(decodeBlockId(0x3D000000u), 0, rules), LootZone::Cave);
}

TEST(ZoneDetectorTest, UnknownAreaAndMaterialUseTheFallback) {
    static constexpr ZoneRule area[] = {{60, LootZone::Grassland}};
    const ZoneRules rules{{}, area, LootZone::Deeproot};

    EXPECT_EQ(classifyZone(decodeBlockId(0x0A000000u), 0, rules), LootZone::Deeproot);
    EXPECT_EQ(classifyZone(decodeBlockId(0), 0, rules), LootZone::Deeproot);
}
