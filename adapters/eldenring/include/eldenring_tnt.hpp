#pragma once

#include "eldenring_blocks.hpp"
#include "mc/types.hpp"
#include "mc/tnt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eldenring::tnt {

struct ErPrimedTnt {
    float pos[3]{0.f, 0.f, 0.f};
    float vel[3]{0.f, 0.f, 0.f};
    float fuse{4.0f};
    float floor_y{-1e9f};
    bool exploded{false};

    [[nodiscard]] float flashFrequency() const noexcept {
        if (fuse > 2.0f) return 2.0f;
        if (fuse > 0.8f) return 4.0f;
        return 8.0f;
    }

    [[nodiscard]] float visualScale() const noexcept {
        float progress = 1.0f - std::clamp(fuse / 4.0f, 0.0f, 1.0f);
        return 1.0f + 0.05f * progress;
    }
};

struct ErTargetMock {
    uint64_t target_id{0};
    float pos[3]{0.f, 0.f, 0.f};
};

struct ErHitDamage {
    uint64_t target_id{0};
    float damage{0.f};
    float knockback{0.f};
    float impulse_dir[3]{0.f, 1.f, 0.f};
};

struct ErExplosionResult {
    float center[3]{0.f, 0.f, 0.f};
    std::vector<blocks::Cell> destroyed_cells;
    std::vector<blocks::Cell> chain_tnt_cells;
    std::vector<ErPrimedTnt> chain_primed_tnt;
};

struct ErExplosionDamageIntent {
    float center[3]{0.f, 0.f, 0.f};
    float radius{8.0f};
    float max_damage{1500.0f};
};

inline int computeTntExplosionDamage(const float center[3], const float target_pos[3], float radius, float max_damage) {
    if (radius <= 0.0f || max_damage <= 0.0f) return 0;
    const float dx = target_pos[0] - center[0];
    const float dy = target_pos[1] - center[1];
    const float dz = target_pos[2] - center[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist > radius) return 0;
    const float exposure = 1.0f - (dist / radius);
    return static_cast<int>(std::round(max_damage * exposure));
}

// Minecraft interaction rules for TNT and Flint & Steel:
// 1. Right-click on a TNT block with Flint & Steel ignites it. Any other held item does NOT ignite it.
inline bool shouldIgniteTntOnRightClick(mc::ItemId held, mc::BlockId target_block) noexcept {
    return (held == mc::ItemId::FlintAndSteel) && (target_block == mc::BlockId::Tnt);
}

// 2. Left-click NEVER ignites TNT (left-click is attack or mine/break block).
inline bool shouldIgniteTntOnLeftClick(mc::ItemId held, mc::BlockId target_block) noexcept {
    (void)held;
    (void)target_block;
    return false;
}

// 3. Left-click on any solid placed block breaks it.
inline bool shouldBreakBlockOnLeftClick(mc::BlockId target_block) noexcept {
    return target_block != mc::BlockId::Air;
}


class ErExplosionIntentQueue {
public:
    void enqueue(const float center[3], float radius = 8.0f, float max_damage = 1500.0f) {
        ErExplosionDamageIntent intent;
        intent.center[0] = center[0];
        intent.center[1] = center[1];
        intent.center[2] = center[2];
        intent.radius = radius;
        intent.max_damage = max_damage;
        intents_.push_back(intent);
    }

    [[nodiscard]] size_t pendingCount() const noexcept {
        return intents_.size();
    }

    [[nodiscard]] std::vector<ErExplosionDamageIntent> drain() {
        std::vector<ErExplosionDamageIntent> out = std::move(intents_);
        intents_.clear();
        return out;
    }

private:
    std::vector<ErExplosionDamageIntent> intents_;
};

class ErTntManager {
public:
    static constexpr float kGravityMPerS2 = -16.0f;
    static constexpr float kAirDrag = 0.98f;

    bool tryIgniteBlock(blocks::BlockGrid& grid, const blocks::Cell& cell, float fuse = 4.0f, const float initial_vel[3] = nullptr) {
        if (grid.get(cell) != mc::BlockId::Tnt) {
            return false;
        }

        grid.remove(cell);

        ErPrimedTnt tnt;
        tnt.pos[0] = static_cast<float>(cell.x) + 0.5f;
        tnt.pos[1] = static_cast<float>(cell.y) + 0.0f;
        tnt.pos[2] = static_cast<float>(cell.z) + 0.5f;

        if (initial_vel != nullptr) {
            tnt.vel[0] = initial_vel[0];
            tnt.vel[1] = initial_vel[1];
            tnt.vel[2] = initial_vel[2];
        } else {
            tnt.vel[0] = 0.0f;
            tnt.vel[1] = 1.5f; // Slight hop on prime
            tnt.vel[2] = 0.0f;
        }

        tnt.fuse = fuse;
        tnt.floor_y = static_cast<float>(cell.y);
        tnt.exploded = false;

        active_tnt_.push_back(tnt);
        return true;
    }

    [[nodiscard]] size_t activeCount() const noexcept {
        return active_tnt_.size();
    }

    [[nodiscard]] const ErPrimedTnt* getTnt(size_t index) const noexcept {
        if (index >= active_tnt_.size()) return nullptr;
        return &active_tnt_[index];
    }

    [[nodiscard]] const std::vector<ErPrimedTnt>& all() const noexcept {
        return active_tnt_;
    }

    [[nodiscard]] std::vector<std::array<float, 3>> activePositions() const {
        std::vector<std::array<float, 3>> out;
        out.reserve(active_tnt_.size());
        for (const auto& t : active_tnt_) {
            if (t.fuse > 0.0f && !t.exploded) {
                out.push_back({t.pos[0], t.pos[1], t.pos[2]});
            }
        }
        return out;
    }

    ErExplosionResult detonateAt(blocks::BlockGrid& grid, const float center[3], float power = 4.0f, float radius = 4.5f, bool add_to_active = true) {
        ErExplosionResult result;
        result.center[0] = center[0];
        result.center[1] = center[1];
        result.center[2] = center[2];

        const int range = static_cast<int>(std::ceil(radius));
        const int cx = static_cast<int>(std::floor(center[0]));
        const int cy = static_cast<int>(std::floor(center[1]));
        const int cz = static_cast<int>(std::floor(center[2]));

        std::vector<blocks::Cell> candidate_chain_tnt;
        std::vector<blocks::Cell> candidate_destructions;

        for (int dx = -range; dx <= range; ++dx) {
            for (int dy = -range; dy <= range; ++dy) {
                for (int dz = -range; dz <= range; ++dz) {
                    blocks::Cell c{cx + dx, cy + dy, cz + dz};
                    const mc::BlockId id = grid.get(c);
                    if (id == mc::BlockId::Air) continue;

                    const float cell_cx = static_cast<float>(c.x) + 0.5f;
                    const float cell_cy = static_cast<float>(c.y) + 0.5f;
                    const float cell_cz = static_cast<float>(c.z) + 0.5f;

                    const float diff_x = cell_cx - center[0];
                    const float diff_y = cell_cy - center[1];
                    const float diff_z = cell_cz - center[2];
                    const float dist = std::sqrt(diff_x * diff_x + diff_y * diff_y + diff_z * diff_z);

                    if (dist > radius) continue;

                    if (id == mc::BlockId::Tnt) {
                        candidate_chain_tnt.push_back(c);
                    } else {
                        const float res = mc::BlockBlastResistance(id);
                        if (res >= 1000.0f) continue; // Obsidian immune

                        const float exposure = 1.0f - (dist / radius);
                        const float energy = power * exposure * 2.0f;
                        if (energy >= (res / 3.0f)) {
                            candidate_destructions.push_back(c);
                        }
                    }
                }
            }
        }

        // Process chain TNT
        for (const auto& c : candidate_chain_tnt) {
            const float cell_cx = static_cast<float>(c.x) + 0.5f;
            const float cell_cy = static_cast<float>(c.y) + 0.5f;
            const float cell_cz = static_cast<float>(c.z) + 0.5f;

            float diff[3] = {cell_cx - center[0], cell_cy - center[1], cell_cz - center[2]};
            float dist = std::sqrt(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
            if (dist < 1e-4f) {
                diff[0] = 0.0f; diff[1] = 1.0f; diff[2] = 0.0f; dist = 1.0f;
            }

            // Short fuse 1.0s (0.5s - 1.5s range) and outward impulse velocity
            float chain_vel[3] = {
                (diff[0] / dist) * 3.0f,
                (diff[1] / dist) * 2.0f + 1.5f,
                (diff[2] / dist) * 3.0f
            };

            grid.remove(c);

            ErPrimedTnt primed;
            primed.pos[0] = cell_cx;
            primed.pos[1] = static_cast<float>(c.y);
            primed.pos[2] = cell_cz;
            primed.vel[0] = chain_vel[0];
            primed.vel[1] = chain_vel[1];
            primed.vel[2] = chain_vel[2];
            primed.fuse = 1.0f;
            primed.floor_y = static_cast<float>(c.y);
            primed.exploded = false;

            result.chain_primed_tnt.push_back(primed);
            if (add_to_active) {
                active_tnt_.push_back(primed);
            }

            result.chain_tnt_cells.push_back(c);
        }

        // Process block destruction
        for (const auto& c : candidate_destructions) {
            grid.remove(c);
            result.destroyed_cells.push_back(c);
        }

        return result;
    }

    std::vector<ErHitDamage> calculateEnemyHits(
        const float blast_center[3],
        const std::vector<ErTargetMock>& targets,
        float max_radius = 8.0f,
        float max_damage = 1500.0f,
        float max_knockback = 25.0f
    ) {
        std::vector<ErHitDamage> hits;

        for (const auto& t : targets) {
            const float diff_x = t.pos[0] - blast_center[0];
            const float diff_y = t.pos[1] - blast_center[1];
            const float diff_z = t.pos[2] - blast_center[2];
            const float dist = std::sqrt(diff_x * diff_x + diff_y * diff_y + diff_z * diff_z);

            if (dist > max_radius) continue;

            const float exposure = std::max(0.0f, 1.0f - (dist / max_radius));
            const float damage = max_damage * exposure;
            const float knockback = max_knockback * exposure;

            ErHitDamage hit;
            hit.target_id = t.target_id;
            hit.damage = damage;
            hit.knockback = knockback;

            if (dist > 1e-4f) {
                hit.impulse_dir[0] = diff_x / dist;
                hit.impulse_dir[1] = diff_y / dist;
                hit.impulse_dir[2] = diff_z / dist;
            } else {
                hit.impulse_dir[0] = 0.0f;
                hit.impulse_dir[1] = 1.0f;
                hit.impulse_dir[2] = 0.0f;
            }

            hits.push_back(hit);
        }

        return hits;
    }

    std::vector<ErExplosionResult> step(float dt, blocks::BlockGrid& grid) {
        std::vector<ErExplosionResult> explosions;
        std::vector<ErPrimedTnt> surviving_tnt;
        std::vector<ErPrimedTnt> pending_chain_tnt;

        for (auto& tnt : active_tnt_) {
            if (tnt.fuse <= 0.0f || tnt.exploded) {
                continue;
            }

            tnt.fuse -= dt;
            if (tnt.fuse <= 0.0f) {
                tnt.fuse = 0.0f;
                tnt.exploded = true;
                ErExplosionResult exp = detonateAt(grid, tnt.pos, 4.0f, 4.5f, false);
                for (const auto& ct : exp.chain_primed_tnt) {
                    pending_chain_tnt.push_back(ct);
                }
                explosions.push_back(std::move(exp));
                continue;
            }

            // Physics step
            tnt.vel[1] += kGravityMPerS2 * dt;
            tnt.vel[0] *= std::pow(kAirDrag, dt * 20.0f);
            tnt.vel[2] *= std::pow(kAirDrag, dt * 20.0f);

            tnt.pos[0] += tnt.vel[0] * dt;
            tnt.pos[1] += tnt.vel[1] * dt;
            tnt.pos[2] += tnt.vel[2] * dt;

            // Simple floor contact with block grid or resting floor_y
            blocks::Cell below = blocks::cellOf(tnt.pos[0], tnt.pos[1] - 0.05f, tnt.pos[2]);
            float min_floor = tnt.floor_y;
            if (grid.get(below) != mc::BlockId::Air) {
                min_floor = std::max(min_floor, static_cast<float>(below.y + 1));
            }
            if (tnt.pos[1] <= min_floor) {
                tnt.pos[1] = min_floor;
                tnt.vel[0] = 0.0f;
                tnt.vel[1] = 0.0f;
                tnt.vel[2] = 0.0f;
            }

            surviving_tnt.push_back(tnt);
        }

        for (auto& ct : pending_chain_tnt) {
            surviving_tnt.push_back(ct);
        }

        active_tnt_ = std::move(surviving_tnt);
        return explosions;
    }

    void clear() {
        active_tnt_.clear();
    }

private:
    std::vector<ErPrimedTnt> active_tnt_;
};

} // namespace eldenring::tnt
