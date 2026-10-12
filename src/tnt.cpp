#include "mc/tnt.hpp"
#include <cmath>

namespace mc {

ExplosionResult CalculateVoxelExplosion(
    const Vec3& blast_center,
    const ExplosionConfig& config,
    const std::function<BlockId(const GridPos&)>& block_lookup,
    const std::vector<ExplosionEntityTarget>& targets
) {
    ExplosionResult result;
    result.center = blast_center;

    const int range = static_cast<int>(std::ceil(config.radius_blocks));
    const int cx = static_cast<int>(std::floor(blast_center.x));
    const int cy = static_cast<int>(std::floor(blast_center.y));
    const int cz = static_cast<int>(std::floor(blast_center.z));

    for (int dx = -range; dx <= range; ++dx) {
        for (int dy = -range; dy <= range; ++dy) {
            for (int dz = -range; dz <= range; ++dz) {
                const GridPos p{cx + dx, cy + dy, cz + dz};
                const float block_cx = p.x + 0.5f;
                const float block_cy = p.y + 0.5f;
                const float block_cz = p.z + 0.5f;

                const float diff_x = block_cx - blast_center.x;
                const float diff_y = block_cy - blast_center.y;
                const float diff_z = block_cz - blast_center.z;
                const float dist = std::sqrt(diff_x * diff_x + diff_y * diff_y + diff_z * diff_z);

                if (dist > config.radius_blocks) continue;

                const BlockId id = block_lookup(p);
                if (id == BlockId::Air) continue;

                if (id == BlockId::Tnt) {
                    result.ignited_tnt_blocks.push_back(p);
                    continue;
                }

                const float resistance = BlockBlastResistance(id);
                if (resistance >= 1000.0f) {
                    // Blast-proof (Obsidian, Bedrock)
                    continue;
                }

                // Blast energy decay
                const float exposure = 1.0f - (dist / config.radius_blocks);
                const float blast_energy = config.power * exposure * 2.0f;
                if (blast_energy >= (resistance / 3.0f)) {
                    result.destroyed_blocks.push_back(p);
                }
            }
        }
    }

    // Entity impacts
    for (const auto& target : targets) {
        const float diff_x = target.position.x - blast_center.x;
        const float diff_y = target.position.y - blast_center.y;
        const float diff_z = target.position.z - blast_center.z;
        const float dist = std::sqrt(diff_x * diff_x + diff_y * diff_y + diff_z * diff_z);

        if (dist > config.radius_blocks) continue;

        const float exposure = std::max(0.0f, 1.0f - (dist / config.radius_blocks));
        const float damage = config.max_damage * exposure;

        Vec3 dir(0.0f, 1.0f, 0.0f);
        if (dist > 1e-4f) {
            dir = Vec3(diff_x / dist, diff_y / dist, diff_z / dist);
        }
        const Vec3 impulse = dir * (config.max_impulse * exposure);

        result.entity_impacts.push_back({target.entity_id, damage, impulse});
    }

    return result;
}

} // namespace mc
