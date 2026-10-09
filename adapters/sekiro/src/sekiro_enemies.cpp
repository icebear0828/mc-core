#include "sekiro_enemies.hpp"

namespace sekiro::live {

namespace {
constexpr float kMaxPlausibleSpeed = 200.0f; // m/s; faster is a teleport, not motion
constexpr uint32_t kEnemyTeamId = 1;         // native::ChrIns::TeamId: 1 = enemy
} // namespace

EnemyTracker::Changes EnemyTracker::update(const std::vector<LiveEnemy>& enemies, float dt) {
    Changes changes;
    for (auto& [id, entry] : entries_) entry.seen = false;

    for (const LiveEnemy& e : enemies) {
        if (!e.hostile || !e.hp_valid || e.dead) continue;

        mc::EntityId found = mc::EntityId::None;
        for (auto& [id, entry] : entries_) {
            if (entry.handle == e.handle && entry.char_id == e.char_id && !entry.seen) {
                found = id;
                break;
            }
        }
        if (found == mc::EntityId::None) {
            found = static_cast<mc::EntityId>(next_id_++);
            Entry entry;
            entry.chr = std::make_unique<native::ChrIns>();
            entry.chr->Handle = static_cast<uint64_t>(found);
            entry.chr->Name = std::to_string(e.char_id);
            entry.chr->TeamId = kEnemyTeamId;
            entry.handle = e.handle;
            entry.char_id = e.char_id;
            entries_.emplace(found, std::move(entry));
            changes.added.push_back(found);
        }

        Entry& entry = entries_.at(found);
        entry.seen = true;
        native::FVector3 velocity{};
        if (entry.have_previous && dt > 1e-4f) {
            velocity = (e.position - entry.previous) * (1.0f / dt);
            if (velocity.Length() > kMaxPlausibleSpeed) velocity = {};
        }
        entry.previous = e.position;
        entry.have_previous = true;
        entry.chr->Position = e.position;
        entry.chr->Velocity = velocity;
        entry.chr->Health = e.hp;
        entry.chr->MaxHealth = e.max_hp;
        entry.chr->bIsDead = false;
    }

    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!it->second.seen) {
            changes.removed.push_back(it->first);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    return changes;
}

std::vector<mc::EntityId> EnemyTracker::clear() {
    std::vector<mc::EntityId> removed;
    for (const auto& [id, entry] : entries_) removed.push_back(id);
    entries_.clear();
    return removed;
}

native::ChrIns* EnemyTracker::mirror(mc::EntityId id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : it->second.chr.get();
}

uintptr_t EnemyTracker::handleOf(mc::EntityId id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? 0 : it->second.handle;
}

} // namespace sekiro::live
