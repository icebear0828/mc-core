#include "mc/ballistics.hpp"
#include "mc/contracts/physics_adapter.hpp"

namespace mc {

BallisticsEngine::BallisticsEngine(IPhysicsAdapter& physics)
    : physics_(physics) {}

uint64_t BallisticsEngine::launch(ProjectileType type, const Vec3& origin, const Vec3& direction, float power) {
    Projectile p;
    p.id = next_id_++;
    p.type = type;
    p.position = origin;
    p.power = power;
    p.age = 0.f;
    p.is_returning = false;

    const Vec3 dir_norm = direction.normalized();

    // Speed in cm/s (e.g. 60m/s = 6000cm/s)
    float base_speed = 6000.f;
    if (type == ProjectileType::Arrow) {
        base_speed = 6000.f * power;
        p.max_lifetime = 60.f;
    } else if (type == ProjectileType::EnderPearl) {
        base_speed = 3000.f;
        p.max_lifetime = 15.f;
    } else if (type == ProjectileType::Trident) {
        base_speed = 5000.f;
        p.max_lifetime = 30.f;
    }

    p.velocity = dir_norm * base_speed;
    projectiles_.push_back(p);
    return p.id;
}

void BallisticsEngine::update(float dt, const Vec3& player_hand_pos) {
    constexpr float kGravity = 980.f; // cm/s^2
    constexpr float kAirResistance = 0.99f;

    for (auto it = projectiles_.begin(); it != projectiles_.end();) {
        it->age += dt;
        if (it->age >= it->max_lifetime) {
            it = projectiles_.erase(it);
            continue;
        }

        if (it->is_returning && it->type == ProjectileType::Trident) {
            // Loyalty return vector acceleration
            const Vec3 delta = player_hand_pos - it->position;
            if (delta.lengthSq() < 50.f * 50.f) {
                // Caught by player hand
                it = projectiles_.erase(it);
                continue;
            }
            it->velocity = it->velocity * 0.95f + delta.normalized() * (1500.f * dt);
            it->position += it->velocity * dt;
            ++it;
            continue;
        }

        // Standard ballistic trajectory step
        it->velocity.z -= (kGravity * dt);
        it->velocity = it->velocity * kAirResistance;

        const Vec3 next_pos = it->position + it->velocity * dt;

        // Swept raycast collision probe
        RaycastResult hit = physics_.raycastWorld(it->position, next_pos);
        if (hit.has_hit) {
            it->position = hit.point;
            if (it->type == ProjectileType::Trident) {
                // Ground hit, enter waiting for Loyalty return state
                it->velocity = {0.f, 0.f, 0.f};
            } else {
                it = projectiles_.erase(it);
                continue;
            }
        } else {
            it->position = next_pos;
        }

        ++it;
    }
}

} // namespace mc
