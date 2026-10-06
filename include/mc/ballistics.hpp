#pragma once

#include "mc/types.hpp"
#include <vector>

namespace mc {

class IPhysicsAdapter;

enum class ProjectileType : uint8_t {
    Arrow,
    EnderPearl,
    Trident
};

struct Projectile {
    uint64_t id{0};
    ProjectileType type{ProjectileType::Arrow};
    Vec3 position{};
    Vec3 velocity{}; // cm/s
    float age{0.f};
    float max_lifetime{30.f};
    float power{1.f};
    bool is_returning{false}; // for Loyalty Trident
};

class BallisticsEngine {
public:
    explicit BallisticsEngine(IPhysicsAdapter& physics);

    uint64_t launch(ProjectileType type, const Vec3& origin, const Vec3& direction, float power);
    void update(float dt, const Vec3& player_hand_pos);

    [[nodiscard]] const std::vector<Projectile>& getActiveProjectiles() const { return projectiles_; }

private:
    IPhysicsAdapter& physics_;
    uint64_t next_id_{1};
    std::vector<Projectile> projectiles_;
};

} // namespace mc
