#pragma once

// Loot-zone detection for ground mining (issue #81). The Elden Ring adapter supplies the player's
// block id (PlayerIns+0x6D0) and the material id of the ground hit; this header decodes the block
// and applies a small rule table. Pure logic, no game headers, unit-tested.

#include "mc/loot_table.hpp"

#include <cstdint>
#include <span>

namespace mc {

struct WorldBlockId {
    uint32_t raw{0};
    uint8_t area{0};
    uint8_t grid_x{0};
    uint8_t grid_z{0};
    uint8_t lod{0};
    bool valid{false};
};

// ER block ids are area<<24 | grid_x<<16 | grid_z<<8 | lod (community-verified format; the
// player's field is PlayerIns+0x6D0). 0 and -1 mean "not in a world block yet".
[[nodiscard]] constexpr WorldBlockId decodeBlockId(uint32_t raw) noexcept {
    WorldBlockId b;
    if (raw == 0 || raw == 0xFFFFFFFFu) return b;
    b.raw = raw;
    b.area = static_cast<uint8_t>(raw >> 24);
    b.grid_x = static_cast<uint8_t>(raw >> 16);
    b.grid_z = static_cast<uint8_t>(raw >> 8);
    b.lod = static_cast<uint8_t>(raw);
    b.valid = true;
    return b;
}

struct ZoneRule {
    uint32_t key; // material id in material_rules, area id in area_rules
    LootZone zone;
};

struct ZoneRules {
    std::span<const ZoneRule> material_rules{}; // checked first
    std::span<const ZoneRule> region_rules{};   // then the raw block id (map tile)
    std::span<const ZoneRule> area_rules{};     // then the block's area byte
    LootZone fallback{LootZone::Grassland};
};

// A material rule wins over the map tile, and the tile wins over the area byte: a stone floor in
// Limgrave should roll the stone table. Unknown or invalid input falls back to `fallback`.
[[nodiscard]] constexpr LootZone classifyZone(const WorldBlockId& block, uint32_t material_id, const ZoneRules& rules) noexcept {
    for (const ZoneRule& r : rules.material_rules) {
        if (r.key == material_id) return r.zone;
    }
    if (block.valid) {
        for (const ZoneRule& r : rules.region_rules) {
            if (r.key == block.raw) return r.zone;
        }
        // The player's block may be a LOD 1/2 variant; fall back to the LOD 0 tile of the same block.
        const uint32_t lod0 = block.raw & 0xFFFFFF00u;
        if (lod0 != block.raw) {
            for (const ZoneRule& r : rules.region_rules) {
                if (r.key == lod0) return r.zone;
            }
        }
        for (const ZoneRule& r : rules.area_rules) {
            if (r.key == block.area) return r.zone;
        }
    }
    return rules.fallback;
}

} // namespace mc
