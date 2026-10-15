#pragma once

// Choosing the enemy under the crosshair for a melee MC attack. Pure geometry on the enemy list (relative positions,
// metres, Havok axes) and the camera ray, so it is unit-tested without the game.

#include "eldenring_world.hpp"
#include "mc/targeting.hpp"

namespace eldenring::live {

using PickParams = mc::PickParams;

inline mc::TargetableEntity toTargetable(const EnemyInfo& e) {
    mc::TargetableEntity t;
    t.id = e.handle != 0 ? e.handle : e.chr;
    t.type_id = e.npc_id;
    t.hp = e.hp;
    t.max_hp = e.max_hp;
    t.rel_pos = {e.rel_x, e.rel_y, e.rel_z};
    t.is_hostile = e.hostile;
    t.is_alive = (e.hp > 0);
    return t;
}

// `cam_rel`: camera position relative to the player's feet; `dir`: unit view direction. Returns the index of the
// best target under the crosshair (prioritizing hostile mobs when both are under reach, or valid neutral/passive animals), or -1.
inline int pickTarget(const float cam_rel[3], const float dir[3], const std::vector<EnemyInfo>& enemies, const PickParams& p = {}) {
    std::vector<mc::TargetableEntity> generic_entities;
    generic_entities.reserve(enemies.size());
    for (const auto& e : enemies) {
        generic_entities.push_back(toTargetable(e));
    }
    return mc::pickTarget(cam_rel, dir, generic_entities, p);
}

} // namespace eldenring::live
