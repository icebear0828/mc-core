#include "mc/terrain_destruction.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <unordered_set>
#include <utility>

namespace mc {
namespace {

constexpr float kBlastRayStep = 0.3f;
constexpr int kBlastRayGrid = 16;      // 16x16x16 rays, as in vanilla
constexpr int kMaxBlastRaySteps = 512; // a ray loses at least 0.09 power per step
constexpr float kMaxCoordinate = 1.0e6f;
constexpr float kBlockRadiusSq = 0.25f; // half a block, squared: "the same column"

float dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cellCenter(const Cell& cell) noexcept {
    return Vec3{static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f,
                static_cast<float>(cell.z) + 0.5f};
}

bool isFinite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool inRange(const Vec3& value) noexcept {
    return std::abs(value.x) <= kMaxCoordinate && std::abs(value.y) <= kMaxCoordinate &&
           std::abs(value.z) <= kMaxCoordinate;
}

uint64_t splitmix64(uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

uint64_t mixCell(uint64_t seed, const Cell& cell) noexcept {
    uint64_t h = splitmix64(seed ^ static_cast<uint64_t>(static_cast<uint32_t>(cell.x)));
    h = splitmix64(h ^ static_cast<uint64_t>(static_cast<uint32_t>(cell.y)));
    h = splitmix64(h ^ static_cast<uint64_t>(static_cast<uint32_t>(cell.z)));
    return h;
}

uint64_t mixRay(uint64_t seed, int i, int j, int k) noexcept {
    const uint64_t key =
        static_cast<uint64_t>(i) | (static_cast<uint64_t>(j) << 8) | (static_cast<uint64_t>(k) << 16);
    return splitmix64(seed ^ key);
}

// [0, 1) from the top 24 bits of a hash.
float unitFloat(uint64_t hash) noexcept {
    return static_cast<float>(hash >> 40) * (1.0f / 16777216.0f);
}

// The blast's jitter and drop rolls are derived from the request itself, never from a global RNG.
uint64_t blastSeed(const BlastRequest& request) noexcept {
    uint64_t h = 0x9E3779B97F4A7C15ull;
    h = splitmix64(h ^ std::bit_cast<uint32_t>(request.center.x));
    h = splitmix64(h ^ std::bit_cast<uint32_t>(request.center.y));
    h = splitmix64(h ^ std::bit_cast<uint32_t>(request.center.z));
    h = splitmix64(h ^ std::bit_cast<uint32_t>(request.power));
    return h;
}

struct DigCandidate {
    Cell cell{};
    BlockId material{BlockId::Air};
    float into{0.0f};
    Vec3 radial{};
    bool bedrock{false};
};

struct DigBlocker {
    float into{0.0f};
    Vec3 radial{};
};

} // namespace

bool isDiggable(const DigRequest& request, const TerrainSample& sample) noexcept {
    if (sample.bedrock || sample.material == BlockId::Air) return false;
    if (!(request.radius > 0.0f) || !(request.depth > 0.0f)) return false;
    if (!isFinite(request.surface_normal)) return false;
    if (!(request.surface_normal.lengthSq() > 1e-8f)) return false;
    return request.tool_level >= requiredToolLevel(terrainFamily(sample.material, sample.bedrock));
}

std::vector<DigCell> planDig(const DigRequest& request, const TerrainSampler& sampler) {
    std::vector<DigCell> plan;
    if (!sampler) return plan;
    if (!std::isfinite(request.radius) || request.radius <= 0.0f || request.radius > kMaxDigRadius) return plan;
    if (!std::isfinite(request.depth) || request.depth <= 0.0f || request.depth > kMaxDigDepth) return plan;
    if (!isFinite(request.hit_point) || !inRange(request.hit_point)) return plan;
    if (!isFinite(request.surface_normal)) return plan;

    const Vec3 normal = request.surface_normal.normalized();
    if (!(normal.lengthSq() >= 0.5f)) return plan; // zero or degenerate normal

    // The cylinder starts at the hit plane and runs into the terrain, opposite the outward normal.
    const float reach = request.depth + kDigEntryBias;
    const Vec3 far_point = request.hit_point - normal * reach;

    const int x0 = static_cast<int>(std::floor(std::min(request.hit_point.x, far_point.x) - request.radius));
    const int x1 = static_cast<int>(std::floor(std::max(request.hit_point.x, far_point.x) + request.radius));
    const int y0 = static_cast<int>(std::floor(std::min(request.hit_point.y, far_point.y) - request.radius));
    const int y1 = static_cast<int>(std::floor(std::max(request.hit_point.y, far_point.y) + request.radius));
    const int z0 = static_cast<int>(std::floor(std::min(request.hit_point.z, far_point.z) - request.radius));
    const int z1 = static_cast<int>(std::floor(std::max(request.hit_point.z, far_point.z) + request.radius));

    std::vector<DigCandidate> candidates;
    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                const Cell cell{x, y, z};
                const Vec3 rel = cellCenter(cell) - request.hit_point;
                const float into = -dot(rel, normal);
                if (!(into >= 0.0f) || into >= reach) continue;
                const Vec3 radial = rel + normal * into;
                if (radial.lengthSq() > request.radius * request.radius) continue;
                const TerrainSample sample = sampler(cell);
                candidates.push_back(DigCandidate{cell, sample.material, into, radial, sample.bedrock});
            }
        }
    }

    // Shallow first, so a bedrock cell is always seen before the cells it can block.
    std::sort(candidates.begin(), candidates.end(), [](const DigCandidate& a, const DigCandidate& b) {
        if (a.into != b.into) return a.into < b.into;
        return cellLess(a.cell, b.cell);
    });

    std::vector<DigBlocker> blockers;
    for (const DigCandidate& candidate : candidates) {
        if (candidate.bedrock) {
            blockers.push_back(DigBlocker{candidate.into, candidate.radial});
            continue;
        }
        const TerrainSample sample{candidate.material, false};
        if (!isDiggable(request, sample)) continue;
        bool blocked = false;
        for (const DigBlocker& blocker : blockers) {
            if (blocker.into < candidate.into && (blocker.radial - candidate.radial).lengthSq() < kBlockRadiusSq) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;
        plan.push_back(DigCell{candidate.cell, candidate.material, terrainLayer(sample, candidate.into)});
    }

    std::sort(plan.begin(), plan.end(), [](const DigCell& a, const DigCell& b) { return cellLess(a.cell, b.cell); });
    return plan;
}

BlastResult planBlast(const BlastRequest& request, const TerrainSampler& sampler,
                      const std::function<float(BlockId)>& blast_resistance) {
    BlastResult result;
    if (!sampler) return result;
    if (!std::isfinite(request.power) || request.power <= 0.0f || request.power > kMaxBlastPower) return result;
    if (!isFinite(request.center) || !inRange(request.center)) return result;

    const uint64_t seed = blastSeed(request);
    std::unordered_set<Cell, CellHash> destroyed;
    std::vector<DigCell> removed;

    for (int i = 0; i < kBlastRayGrid; ++i) {
        for (int j = 0; j < kBlastRayGrid; ++j) {
            for (int k = 0; k < kBlastRayGrid; ++k) {
                // Vanilla casts only the rays on the shell of the 16x16x16 grid.
                const bool on_shell = i == 0 || i == kBlastRayGrid - 1 || j == 0 || j == kBlastRayGrid - 1 ||
                                      k == 0 || k == kBlastRayGrid - 1;
                if (!on_shell) continue;
                const Vec3 raw{static_cast<float>(i) * (2.0f / 15.0f) - 1.0f,
                               static_cast<float>(j) * (2.0f / 15.0f) - 1.0f,
                               static_cast<float>(k) * (2.0f / 15.0f) - 1.0f};
                const float length_sq = raw.lengthSq();
                if (!(length_sq > 1e-8f)) continue;
                const Vec3 direction = raw * (1.0f / std::sqrt(length_sq));
                float remaining = request.power * (0.7f + unitFloat(mixRay(seed, i, j, k)) * 0.6f);

                Vec3 point = request.center;
                for (int step = 0; step < kMaxBlastRaySteps && remaining > 0.0f; ++step) {
                    const Cell cell = cellOf(point.x, point.y, point.z);
                    if (destroyed.find(cell) != destroyed.end()) {
                        remaining -= 0.3f * 0.3f; // already air: only the base step cost
                    } else {
                        const TerrainSample sample = sampler(cell);
                        if (sample.bedrock) break;
                        float resistance = blast_resistance ? blast_resistance(sample.material)
                                                            : BlockBlastResistance(sample.material);
                        if (!(resistance >= 0.0f)) resistance = 0.0f; // rejects NaN and negatives
                        remaining -= (resistance + 0.3f) * 0.3f;
                        if (remaining > 0.0f && sample.material != BlockId::Air) {
                            destroyed.insert(cell);
                            removed.push_back(DigCell{cell, sample.material, terrainLayer(sample, 0.0f)});
                        }
                    }
                    point = point + direction * kBlastRayStep;
                }
            }
        }
    }

    std::sort(removed.begin(), removed.end(),
              [](const DigCell& a, const DigCell& b) { return cellLess(a.cell, b.cell); });
    removed.erase(std::unique(removed.begin(), removed.end(),
                              [](const DigCell& a, const DigCell& b) { return a.cell == b.cell; }),
                  removed.end());

    float radius = 0.0f;
    for (const DigCell& cell : removed) {
        radius = std::max(radius, (cellCenter(cell.cell) - request.center).length());
    }

    // Vanilla: each destroyed block has a 1/power chance to drop itself.
    std::map<ItemId, uint32_t> counts;
    const float chance = 1.0f / request.power;
    for (const DigCell& cell : removed) {
        const ItemId item = itemForBlock(cell.material);
        if (item == ItemId::None) continue;
        if (unitFloat(mixCell(seed, cell.cell)) >= chance) continue;
        ++counts[item];
    }

    result.removed = std::move(removed);
    result.radius = radius;
    result.drops.reserve(counts.size());
    for (const auto& entry : counts) {
        result.drops.push_back(ItemStack{entry.first, entry.second});
    }
    return result;
}

} // namespace mc
