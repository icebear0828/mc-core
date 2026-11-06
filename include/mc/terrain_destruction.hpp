#pragma once

// Deterministic terrain-destruction specs: a dig or a blast becomes a concrete, sorted set of voxels
// an adapter removes from its own world (SkyCraft's Dig/DigBlast behaviour). Everything here is pure
// data; the only host interaction is the sampling callback the adapter supplies. The same request
// against the same terrain always produces the same plan.

#include "mc/blocks.hpp"
#include "mc/inventory.hpp"
#include "mc/tnt.hpp"
#include "mc/types.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace mc {

// Layer index reported on a DigCell.
inline constexpr int kTerrainLayerSurface = 0; // first block of the column the dig broke
inline constexpr int kTerrainLayerSubsoil = 1; // soil below the surface block
inline constexpr int kTerrainLayerStone = 2;
inline constexpr int kTerrainLayerOre = 3;     // host-specific vein material, passed through unchanged
inline constexpr int kTerrainLayerBedrock = 4; // never removed

// Half a block: the surface cell's centre sits this far behind the hit plane, so the near cap of the
// dig cylinder is pulled back by this much.
inline constexpr float kDigEntryBias = 0.5f;
// Soil whose centre is closer than this to the hit plane (metres into the terrain) is the surface.
inline constexpr float kSurfaceSoilDepth = 1.0f;

// Sanity caps: a request outside these is rejected instead of scanning an unbounded volume.
inline constexpr float kMaxDigRadius = 64.0f;
inline constexpr float kMaxDigDepth = 64.0f;
inline constexpr float kMaxBlastPower = 64.0f;

// What the host reports at one cell. `bedrock` is the adapter's own unbreakable flag: such a cell is
// never removed and stops a dig column or a blast ray.
struct TerrainSample {
    BlockId material{BlockId::Air};
    bool bedrock{false};
};

using TerrainSampler = std::function<TerrainSample(const Cell&)>;

// Material family that decides the layer and the tool tier a dig needs. Anything outside the soil and
// stone families (a host ore, a built block) is a vein: it is carved as itself, never converted.
enum class TerrainFamily : uint8_t { Soil, Stone, Ore, Bedrock };

inline constexpr TerrainFamily terrainFamily(BlockId material, bool bedrock) noexcept {
    if (bedrock) return TerrainFamily::Bedrock;
    switch (material) {
        case BlockId::Dirt:
        case BlockId::Sand:
        case BlockId::Gravel: return TerrainFamily::Soil;
        case BlockId::Stone:
        case BlockId::Cobblestone:
        case BlockId::StoneBricks:
        case BlockId::Bricks:
        case BlockId::Obsidian: return TerrainFamily::Stone;
        default: return TerrainFamily::Ore;
    }
}

// Minimum DigRequest::tool_level that can carve a family. Bedrock is never carvable.
inline constexpr int requiredToolLevel(TerrainFamily family) noexcept {
    switch (family) {
        case TerrainFamily::Soil: return 0;
        case TerrainFamily::Stone: return 1;
        case TerrainFamily::Ore: return 2;
        case TerrainFamily::Bedrock: break;
    }
    return 99;
}

// Layer of a sampled cell whose centre lies `depth` metres into the terrain along the dig axis.
// A blast has no axis and passes 0, so its soil reports as the surface layer.
inline constexpr int terrainLayer(const TerrainSample& sample, float depth) noexcept {
    const TerrainFamily family = terrainFamily(sample.material, sample.bedrock);
    if (family == TerrainFamily::Bedrock) return kTerrainLayerBedrock;
    if (family == TerrainFamily::Ore) return kTerrainLayerOre;
    if (family == TerrainFamily::Stone) return kTerrainLayerStone;
    return depth < kSurfaceSoilDepth ? kTerrainLayerSurface : kTerrainLayerSubsoil;
}

// Total order over cells so plans sort deterministically.
inline constexpr bool cellLess(const Cell& a, const Cell& b) noexcept {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}

struct DigRequest {
    Vec3 hit_point{};
    Vec3 surface_normal{0.0f, 1.0f, 0.0f}; // outward: points away from the ground
    float radius{0.5f};
    float depth{1.0f};
    int tool_level{1};
};

struct DigCell {
    Cell cell{};
    BlockId material{BlockId::Air};
    int layer{kTerrainLayerSurface};
};

struct BlastRequest {
    Vec3 center{};
    float power{4.0f};
};

struct BlastResult {
    std::vector<DigCell> removed;
    std::vector<ItemStack> drops;
    float radius{0.0f}; // distance from the centre to the farthest removed cell centre
};

// False for bedrock, air, a degenerate request (non-positive radius or depth, non-finite or zero
// normal) and a material the tool tier cannot carve.
[[nodiscard]] bool isDiggable(const DigRequest& request, const TerrainSample& sample) noexcept;

// Carves a cylinder of `radius` around the axis from the hit point along -surface_normal, `depth`
// metres deep (kDigEntryBias covers the surface cell's centre sitting half a block behind the hit
// plane). Bedrock is never emitted and stops the rest of its column. Sorted by cell, each cell once.
[[nodiscard]] std::vector<DigCell> planDig(const DigRequest& request, const TerrainSampler& sampler);

// Vanilla-style blast: 16x16x16 rays march outwards in 0.3 m steps, each starting at
// power * (0.7 + seeded jitter * 0.6) and losing (resistance + 0.3) * 0.3 per step; a cell is
// destroyed while the remaining power stays positive. `blast_resistance` may be empty, which falls
// back to BlockBlastResistance. Drops use a 1/power chance seeded by the request, so they are
// deterministic. `removed` is sorted and unique; bedrock never appears.
[[nodiscard]] BlastResult planBlast(const BlastRequest& request, const TerrainSampler& sampler,
                                    const std::function<float(BlockId)>& blast_resistance);

} // namespace mc
