#pragma once

#include "mc/types.hpp"
#include <cstdint>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>

namespace mc {

// Blast resistance matching Minecraft Java Edition specifications
inline constexpr float BlockBlastResistance(BlockId block) noexcept {
    switch (block) {
        case BlockId::Air: return 0.0f;
        case BlockId::Tnt: return 0.0f;
        case BlockId::Dirt: return 0.5f;
        case BlockId::Wood: return 2.0f;
        case BlockId::CraftingTable: return 2.5f;
        case BlockId::Sand: return 0.5f;
        case BlockId::Gravel: return 0.6f;
        case BlockId::WoodPlanks: return 3.0f;
        case BlockId::StoneBricks: return 6.0f;
        case BlockId::Bricks: return 6.0f;
        case BlockId::WhiteWool: return 0.8f;
        case BlockId::Anvil: return 1200.0f;
        case BlockId::Stone: return 6.0f;
        case BlockId::Obsidian: return 1200.0f;
        default: return 1.0f;
    }
}

class PrimedTnt {
public:
    static constexpr float kGravityCmS2 = -1600.0f; // -16 m/s^2 in cm/s^2
    static constexpr float kDrag = 0.98f;

    PrimedTnt(uint64_t id, const Vec3& pos, float fuse_seconds = 4.0f, const Vec3& vel = Vec3(0.0f, 150.0f, 0.0f))
        : id_(id), pos_(pos), vel_(vel), fuse_seconds_(fuse_seconds) {}

    void update(float dt, float ground_y) {
        if (fuse_seconds_ <= 0.0f) {
            exploded_ = true;
            return;
        }

        fuse_seconds_ -= dt;
        if (fuse_seconds_ <= 0.0f) {
            fuse_seconds_ = 0.0f;
            exploded_ = true;
            return;
        }

        // Kinematics integration
        vel_.y += kGravityCmS2 * dt;
        vel_.x *= std::pow(kDrag, dt * 20.0f);
        vel_.z *= std::pow(kDrag, dt * 20.0f);

        pos_ = pos_ + vel_ * dt;

        // Ground collision
        if (pos_.y <= ground_y) {
            pos_.y = ground_y;
            vel_.y = 0.0f;
            vel_.x = 0.0f;
            vel_.z = 0.0f;
        }
    }

    [[nodiscard]] uint64_t getId() const noexcept { return id_; }
    [[nodiscard]] const Vec3& getPosition() const noexcept { return pos_; }
    [[nodiscard]] const Vec3& getVelocity() const noexcept { return vel_; }
    [[nodiscard]] float getFuseSeconds() const noexcept { return fuse_seconds_; }
    [[nodiscard]] bool hasExploded() const noexcept { return exploded_; }

    [[nodiscard]] float getFlashFrequency() const noexcept {
        if (fuse_seconds_ > 2.0f) {
            return 2.0f; // 2 Hz early
        } else if (fuse_seconds_ > 0.8f) {
            return 4.0f; // 4 Hz mid
        } else {
            return 8.0f; // 8 Hz late
        }
    }

    [[nodiscard]] float getVisualScale() const noexcept {
        float progress = 1.0f - std::clamp(fuse_seconds_ / 4.0f, 0.0f, 1.0f);
        return 1.0f + 0.05f * progress;
    }

private:
    uint64_t id_{0};
    Vec3 pos_{};
    Vec3 vel_{};
    float fuse_seconds_{4.0f};
    bool exploded_{false};
};

struct ExplosionConfig {
    float power{4.0f};          // Standard MC TNT power
    float radius_blocks{4.5f};  // Block destruction radius
    float max_damage{65.0f};    // Max entity damage
    float max_impulse{20.0f};   // Max knockback impulse
};

struct ExplosionEntityTarget {
    EntityId entity_id{EntityId::None};
    Vec3 position{};
};

struct EntityImpact {
    EntityId entity_id{EntityId::None};
    float damage{0.0f};
    Vec3 impulse{};
};

struct ExplosionResult {
    Vec3 center{};
    std::vector<GridPos> destroyed_blocks;
    std::vector<GridPos> ignited_tnt_blocks;
    std::vector<EntityImpact> entity_impacts;
};

ExplosionResult CalculateVoxelExplosion(
    const Vec3& blast_center,
    const ExplosionConfig& config,
    const std::function<BlockId(const GridPos&)>& block_lookup,
    const std::vector<ExplosionEntityTarget>& targets
);

// Minecraft interaction rules for TNT and Flint & Steel:
// 1. Right-click on a TNT block with Flint & Steel ignites it. Any other held item does NOT ignite it.
inline bool shouldIgniteTntOnRightClick(ItemId held, BlockId target_block) noexcept {
    return (held == ItemId::FlintAndSteel) && (target_block == BlockId::Tnt);
}

// 2. Left-click NEVER ignites TNT (left-click is attack or mine/break block).
inline bool shouldIgniteTntOnLeftClick(ItemId held, BlockId target_block) noexcept {
    (void)held;
    (void)target_block;
    return false;
}

// 3. Left-click on any solid placed block breaks it.
inline bool shouldBreakBlockOnLeftClick(BlockId target_block) noexcept {
    return target_block != BlockId::Air;
}

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

} // namespace mc

