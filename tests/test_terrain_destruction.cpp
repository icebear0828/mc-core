#include <gtest/gtest.h>

#include "mc/terrain_destruction.hpp"
#include "mc/tnt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mc {
namespace {

float dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cellCenter(const Cell& c) noexcept {
    return Vec3{static_cast<float>(c.x) + 0.5f, static_cast<float>(c.y) + 0.5f, static_cast<float>(c.z) + 0.5f};
}

// Distance from the hit plane into the terrain, measured along the outward surface normal.
float depthInto(const Vec3& hit, const Vec3& normal, const Cell& c) noexcept {
    return -dot(cellCenter(c) - hit, normal);
}

bool insideDigVolume(const DigRequest& request, const Cell& c) {
    const Vec3 normal = request.surface_normal.normalized();
    const Vec3 rel = cellCenter(c) - request.hit_point;
    const float into = -dot(rel, normal);
    if (into < 0.0f || into >= request.depth + kDigEntryBias) return false;
    const Vec3 radial = rel + normal * into;
    return radial.lengthSq() <= request.radius * request.radius + 1e-4f;
}

// Flat world: air above y = 63, dirt down to y = 60, stone below, bedrock at the bottom.
// The walkable surface is the top face of y = 63, at y = 64.
TerrainSample flatSample(const Cell& c) {
    if (c.y >= 64) return TerrainSample{BlockId::Air, false};
    if (c.y >= 60) return TerrainSample{BlockId::Dirt, false};
    if (c.y >= 1) return TerrainSample{BlockId::Stone, false};
    return TerrainSample{BlockId::Stone, true};
}

TerrainSampler flatWorld() { return TerrainSampler(&flatSample); }

TerrainSampler uniformStoneWorld() {
    return [](const Cell& c) -> TerrainSample {
        if (c.y >= 64) return TerrainSample{BlockId::Air, false};
        return TerrainSample{BlockId::Stone, false};
    };
}

TerrainSampler uniformDirtWorld() {
    return [](const Cell& c) -> TerrainSample {
        if (c.y >= 64) return TerrainSample{BlockId::Air, false};
        return TerrainSample{BlockId::Dirt, false};
    };
}

bool containsCell(const std::vector<DigCell>& cells, int x, int y, int z) {
    for (const DigCell& c : cells) {
        if (c.cell.x == x && c.cell.y == y && c.cell.z == z) return true;
    }
    return false;
}

const DigCell* findCell(const std::vector<DigCell>& cells, int x, int y, int z) {
    for (const DigCell& c : cells) {
        if (c.cell.x == x && c.cell.y == y && c.cell.z == z) return &c;
    }
    return nullptr;
}

DigRequest flatDig() {
    DigRequest request;
    request.hit_point = Vec3(0.5f, 64.0f, 0.5f);
    request.surface_normal = Vec3(0.0f, 1.0f, 0.0f);
    request.radius = 1.0f;
    request.depth = 2.0f;
    request.tool_level = 1;
    return request;
}

BlastRequest dirtBlast(float power) {
    BlastRequest request;
    request.center = Vec3(0.5f, 63.5f, 0.5f);
    request.power = power;
    return request;
}

} // namespace

TEST(TerrainDestructionTest, FlatDigRemovesCylinderOfSurfaceCells) {
    const DigRequest request = flatDig();
    const std::vector<DigCell> plan = planDig(request, flatWorld());

    // radius 1.0 carves the axis cell plus its four edge neighbours; depth 2.0 carves two layers.
    ASSERT_EQ(plan.size(), 10u);
    for (int y : {63, 62}) {
        EXPECT_TRUE(containsCell(plan, 0, y, 0));
        EXPECT_TRUE(containsCell(plan, 1, y, 0));
        EXPECT_TRUE(containsCell(plan, -1, y, 0));
        EXPECT_TRUE(containsCell(plan, 0, y, 1));
        EXPECT_TRUE(containsCell(plan, 0, y, -1));
    }
    // The diagonal neighbours sit at sqrt(2) from the axis, outside the radius.
    EXPECT_FALSE(containsCell(plan, 1, 63, 1));
    EXPECT_FALSE(containsCell(plan, -1, 62, -1));
    // A third layer would need depth >= 3.
    EXPECT_FALSE(containsCell(plan, 0, 61, 0));

    for (const DigCell& cell : plan) {
        EXPECT_TRUE(insideDigVolume(request, cell.cell));
        EXPECT_EQ(cell.material, BlockId::Dirt);
    }
}

TEST(TerrainDestructionTest, LayeringAssignsSurfaceSubsoilStoneOreAndBedrock) {
    const TerrainSampler column = [](const Cell& c) -> TerrainSample {
        switch (c.y) {
            case 63: return TerrainSample{BlockId::Dirt, false};
            case 62: return TerrainSample{BlockId::Dirt, false};
            case 61: return TerrainSample{BlockId::Stone, false};
            case 60: return TerrainSample{BlockId::Wood, false}; // host vein: passed through unchanged
            case 59: return TerrainSample{BlockId::Stone, true};
            default: return TerrainSample{BlockId::Air, false};
        }
    };

    DigRequest request;
    request.hit_point = Vec3(0.5f, 64.0f, 0.5f);
    request.surface_normal = Vec3(0.0f, 1.0f, 0.0f);
    request.radius = 0.5f;
    request.depth = 5.0f;
    request.tool_level = 2; // the vein needs the second tier

    const std::vector<DigCell> plan = planDig(request, column);
    ASSERT_EQ(plan.size(), 4u);

    const DigCell* surface = findCell(plan, 0, 63, 0);
    const DigCell* subsoil = findCell(plan, 0, 62, 0);
    const DigCell* stone = findCell(plan, 0, 61, 0);
    const DigCell* ore = findCell(plan, 0, 60, 0);
    ASSERT_NE(surface, nullptr);
    ASSERT_NE(subsoil, nullptr);
    ASSERT_NE(stone, nullptr);
    ASSERT_NE(ore, nullptr);

    EXPECT_EQ(surface->layer, kTerrainLayerSurface);
    EXPECT_EQ(subsoil->layer, kTerrainLayerSubsoil);
    EXPECT_EQ(stone->layer, kTerrainLayerStone);
    EXPECT_EQ(ore->layer, kTerrainLayerOre);
    EXPECT_EQ(ore->material, BlockId::Wood);
    EXPECT_EQ(findCell(plan, 0, 59, 0), nullptr); // bedrock is never removed

    EXPECT_EQ(terrainLayer(TerrainSample{BlockId::Stone, true}, 0.0f), kTerrainLayerBedrock);
    EXPECT_EQ(terrainLayer(TerrainSample{BlockId::Dirt, false}, 0.5f), kTerrainLayerSurface);
    EXPECT_EQ(terrainLayer(TerrainSample{BlockId::Dirt, false}, 1.5f), kTerrainLayerSubsoil);
    EXPECT_EQ(terrainLayer(TerrainSample{BlockId::Stone, false}, 0.5f), kTerrainLayerStone);
}

TEST(TerrainDestructionTest, BedrockIsNeverRemovedAndStopsTheDig) {
    const TerrainSampler with_bedrock = [](const Cell& c) -> TerrainSample {
        if (c.y == 63) return TerrainSample{BlockId::Dirt, false};
        if (c.y == 62) return TerrainSample{BlockId::Stone, true};
        if (c.y <= 61) return TerrainSample{BlockId::Stone, false};
        return TerrainSample{BlockId::Air, false};
    };

    DigRequest request;
    request.hit_point = Vec3(0.5f, 64.0f, 0.5f);
    request.surface_normal = Vec3(0.0f, 1.0f, 0.0f);
    request.radius = 0.5f;
    request.depth = 3.0f;

    const std::vector<DigCell> plan = planDig(request, with_bedrock);
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0].cell.y, 63);

    // The same column without the bedrock layer keeps digging.
    const std::vector<DigCell> open = planDig(request, uniformStoneWorld());
    EXPECT_EQ(open.size(), 3u);
    EXPECT_TRUE(containsCell(open, 0, 61, 0));
}

TEST(TerrainDestructionTest, AngledNormalSkewsTheCarvedCells) {
    DigRequest vertical = flatDig();
    vertical.radius = 0.75f;
    vertical.depth = 3.0f;

    DigRequest angled = vertical;
    angled.surface_normal = Vec3(0.6f, 0.8f, 0.0f); // tilts towards +x, so the scoop leans towards -x

    const TerrainSampler world = uniformStoneWorld();
    const std::vector<DigCell> straight = planDig(vertical, world);
    const std::vector<DigCell> skewed = planDig(angled, world);

    ASSERT_FALSE(skewed.empty());
    ASSERT_FALSE(straight.empty());

    // Every carved cell stays inside the cylinder, whatever the normal.
    for (const DigCell& cell : skewed) {
        EXPECT_TRUE(insideDigVolume(angled, cell.cell));
    }

    // The vertical dig is a straight column at x = 0; the angled one walks towards -x with depth.
    for (const DigCell& cell : straight) {
        EXPECT_EQ(cell.cell.x, 0);
        EXPECT_EQ(cell.cell.z, 0);
    }

    const Vec3 normal = angled.surface_normal.normalized();
    float sum_x = 0.0f;
    for (const DigCell& cell : skewed) {
        sum_x += static_cast<float>(cell.cell.x);
    }
    const float mean_x = sum_x / static_cast<float>(skewed.size());
    EXPECT_LT(mean_x, -0.5f);

    // The deepest carved cell is offset from the shallowest one along -x.
    const DigCell* shallowest = &skewed.front();
    const DigCell* deepest = &skewed.front();
    for (const DigCell& cell : skewed) {
        if (depthInto(angled.hit_point, normal, cell.cell) < depthInto(angled.hit_point, normal, shallowest->cell)) {
            shallowest = &cell;
        }
        if (depthInto(angled.hit_point, normal, cell.cell) > depthInto(angled.hit_point, normal, deepest->cell)) {
            deepest = &cell;
        }
    }
    EXPECT_LE(deepest->cell.x, shallowest->cell.x - 1);
    EXPECT_NE(straight.size(), skewed.size());
}

TEST(TerrainDestructionTest, IsDiggableRejectsBedrockAndInvalidNormal) {
    DigRequest request = flatDig();

    EXPECT_TRUE(isDiggable(request, TerrainSample{BlockId::Dirt, false}));
    EXPECT_FALSE(isDiggable(request, TerrainSample{BlockId::Stone, true}));
    EXPECT_FALSE(isDiggable(request, TerrainSample{BlockId::Air, false}));

    request.surface_normal = Vec3(0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(isDiggable(request, TerrainSample{BlockId::Dirt, false}));
    EXPECT_TRUE(planDig(request, flatWorld()).empty());

    request.surface_normal = Vec3(std::nanf(""), 1.0f, 0.0f);
    EXPECT_FALSE(isDiggable(request, TerrainSample{BlockId::Dirt, false}));
    EXPECT_TRUE(planDig(request, flatWorld()).empty());

    request = flatDig();
    request.radius = 0.0f;
    EXPECT_FALSE(isDiggable(request, TerrainSample{BlockId::Dirt, false}));
    EXPECT_TRUE(planDig(request, flatWorld()).empty());
}

TEST(TerrainDestructionTest, ToolLevelGatesStoneAndVeins) {
    DigRequest request = flatDig();

    request.tool_level = 0;
    EXPECT_TRUE(isDiggable(request, TerrainSample{BlockId::Dirt, false}));
    EXPECT_FALSE(isDiggable(request, TerrainSample{BlockId::Stone, false}));

    request.tool_level = 1;
    EXPECT_TRUE(isDiggable(request, TerrainSample{BlockId::Stone, false}));
    EXPECT_FALSE(isDiggable(request, TerrainSample{BlockId::Wood, false}));

    request.tool_level = 2;
    EXPECT_TRUE(isDiggable(request, TerrainSample{BlockId::Wood, false}));
}

TEST(TerrainDestructionTest, IdenticalRequestsProduceIdenticalPlans) {
    const DigRequest dig = flatDig();
    const std::vector<DigCell> first = planDig(dig, flatWorld());
    const std::vector<DigCell> second = planDig(dig, flatWorld());
    ASSERT_EQ(first.size(), second.size());
    for (size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].cell, second[i].cell);
        EXPECT_EQ(first[i].material, second[i].material);
        EXPECT_EQ(first[i].layer, second[i].layer);
    }

    const BlastRequest blast = dirtBlast(4.0f);
    const auto resistance = [](BlockId id) { return BlockBlastResistance(id); };
    const BlastResult a = planBlast(blast, uniformDirtWorld(), resistance);
    const BlastResult b = planBlast(blast, uniformDirtWorld(), resistance);
    ASSERT_EQ(a.removed.size(), b.removed.size());
    ASSERT_EQ(a.drops.size(), b.drops.size());
    EXPECT_FLOAT_EQ(a.radius, b.radius);
    for (size_t i = 0; i < a.removed.size(); ++i) {
        EXPECT_EQ(a.removed[i].cell, b.removed[i].cell);
    }
    for (size_t i = 0; i < a.drops.size(); ++i) {
        EXPECT_EQ(a.drops[i].item, b.drops[i].item);
        EXPECT_EQ(a.drops[i].count, b.drops[i].count);
    }
}

TEST(TerrainDestructionTest, BlastRemovesCellsInsideItsRadius) {
    const BlastRequest request = dirtBlast(4.0f);
    const BlastResult result = planBlast(request, uniformDirtWorld(),
                                         [](BlockId id) { return BlockBlastResistance(id); });

    ASSERT_FALSE(result.removed.empty());
    EXPECT_GT(result.radius, 0.0f);
    // A ray loses at least 0.09 power per 0.3 m step, so nothing can sit beyond 4.33 * power.
    const float max_reach = 4.34f * request.power + 1.0f;
    EXPECT_LE(result.radius, max_reach);

    bool near_centre = false;
    for (const DigCell& cell : result.removed) {
        EXPECT_EQ(cell.material, BlockId::Dirt);
        const float distance = (cellCenter(cell.cell) - request.center).length();
        EXPECT_LE(distance, result.radius + 1e-3f);
        EXPECT_LE(distance, max_reach);
        if (distance <= 1.0f) near_centre = true;
    }
    EXPECT_TRUE(near_centre);
}

TEST(TerrainDestructionTest, BlastKeepsHighResistanceCells) {
    // Everything from x = 2 outwards is obsidian: the rays stop there and the wall survives.
    const TerrainSampler walled = [](const Cell& c) -> TerrainSample {
        if (c.y >= 64) return TerrainSample{BlockId::Air, false};
        if (c.x >= 2) return TerrainSample{BlockId::Obsidian, false};
        return TerrainSample{BlockId::Dirt, false};
    };

    const BlastRequest request = dirtBlast(4.0f);
    const BlastResult result = planBlast(request, walled, [](BlockId id) { return BlockBlastResistance(id); });

    ASSERT_FALSE(result.removed.empty());
    for (const DigCell& cell : result.removed) {
        EXPECT_NE(cell.material, BlockId::Obsidian);
        EXPECT_LT(cell.cell.x, 2);
    }
}

TEST(TerrainDestructionTest, StrongerBlastRemovesStrictlyMoreCells) {
    const auto resistance = [](BlockId id) { return BlockBlastResistance(id); };
    const BlastResult weak = planBlast(dirtBlast(2.0f), uniformDirtWorld(), resistance);
    const BlastResult strong = planBlast(dirtBlast(4.0f), uniformDirtWorld(), resistance);

    ASSERT_FALSE(weak.removed.empty());
    ASSERT_FALSE(strong.removed.empty());
    EXPECT_GT(strong.removed.size(), weak.removed.size());
    EXPECT_GT(strong.radius, weak.radius);
}

TEST(TerrainDestructionTest, BlastDropsAreDeterministicAndOnlyDestroyedMaterials) {
    // Dirt on top of stone so the crater spans two materials.
    const TerrainSampler layered = [](const Cell& c) -> TerrainSample {
        if (c.y >= 64) return TerrainSample{BlockId::Air, false};
        if (c.y >= 62) return TerrainSample{BlockId::Dirt, false};
        return TerrainSample{BlockId::Stone, false};
    };

    const BlastRequest request = dirtBlast(4.0f);
    const auto resistance = [](BlockId id) { return BlockBlastResistance(id); };
    const BlastResult first = planBlast(request, layered, resistance);
    const BlastResult second = planBlast(request, layered, resistance);

    ASSERT_FALSE(first.removed.empty());
    ASSERT_FALSE(first.drops.empty());
    ASSERT_EQ(first.drops.size(), second.drops.size());

    uint32_t dropped = 0;
    for (size_t i = 0; i < first.drops.size(); ++i) {
        EXPECT_EQ(first.drops[i].item, second.drops[i].item);
        EXPECT_EQ(first.drops[i].count, second.drops[i].count);
        EXPECT_NE(first.drops[i].item, ItemId::None);
        EXPECT_GT(first.drops[i].count, 0u);
        dropped += first.drops[i].count;
    }
    EXPECT_LE(dropped, static_cast<uint32_t>(first.removed.size()));

    // Every drop maps back to a material the blast actually destroyed.
    for (const ItemStack& stack : first.drops) {
        bool matched = false;
        for (const DigCell& cell : first.removed) {
            if (itemForBlock(cell.material) == stack.item) matched = true;
        }
        EXPECT_TRUE(matched);
    }
    // Drops are aggregated and sorted by item id.
    for (size_t i = 1; i < first.drops.size(); ++i) {
        EXPECT_LT(static_cast<uint16_t>(first.drops[i - 1].item), static_cast<uint16_t>(first.drops[i].item));
    }
}

TEST(TerrainDestructionTest, BlastResistanceCallbackOverridesVanillaValues) {
    const BlastRequest request = dirtBlast(4.0f);
    const BlastResult tough = planBlast(request, uniformDirtWorld(), [](BlockId) { return 1200.0f; });
    EXPECT_TRUE(tough.removed.empty());
    EXPECT_TRUE(tough.drops.empty());
    EXPECT_FLOAT_EQ(tough.radius, 0.0f);
}

TEST(TerrainDestructionTest, PlansAreSortedUniqueAndContainNoAir) {
    const std::vector<DigCell> plan = planDig(flatDig(), flatWorld());
    ASSERT_FALSE(plan.empty());
    for (size_t i = 1; i < plan.size(); ++i) {
        EXPECT_TRUE(cellLess(plan[i - 1].cell, plan[i].cell));
        EXPECT_FALSE(plan[i - 1].cell == plan[i].cell);
    }
    for (const DigCell& cell : plan) {
        EXPECT_NE(cell.material, BlockId::Air);
    }

    const BlastResult blast = planBlast(dirtBlast(4.0f), uniformDirtWorld(),
                                        [](BlockId id) { return BlockBlastResistance(id); });
    ASSERT_FALSE(blast.removed.empty());
    for (size_t i = 1; i < blast.removed.size(); ++i) {
        EXPECT_TRUE(cellLess(blast.removed[i - 1].cell, blast.removed[i].cell));
    }
    for (const DigCell& cell : blast.removed) {
        EXPECT_NE(cell.material, BlockId::Air);
    }
}

} // namespace mc
