#pragma once

// Turns the per-frame list of enemies read from the live game into stable EntityIds and adapter-owned mirror
// objects. The host's addresses are only an identity within a frame (the game recycles slots); the ids handed
// to the core are small integers that are never reused.

#include "mc/types.hpp"
#include "sekiro_live.hpp"
#include "sekiro_native.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace sekiro::live {

class EnemyTracker {
public:
    struct Changes {
        std::vector<mc::EntityId> added;
        std::vector<mc::EntityId> removed;
    };

    // Targets are living, hostile enemies whose health is known. Anyone else (neutral, dead, unreadable) is
    // not tracked, so a corpse stops being hittable the frame its health reads 0.
    Changes update(const std::vector<LiveEnemy>& enemies, float dt);
    // Everything removed (world unloaded, Steve mode off).
    std::vector<mc::EntityId> clear();

    // Stable for the lifetime of the entry: the adapter keeps this pointer.
    [[nodiscard]] native::ChrIns* mirror(mc::EntityId id) const;
    [[nodiscard]] uintptr_t handleOf(mc::EntityId id) const; // 0 when unknown
    [[nodiscard]] size_t count() const { return entries_.size(); }

private:
    struct Entry {
        std::unique_ptr<native::ChrIns> chr;
        uintptr_t handle{0};
        uint32_t char_id{0};
        native::FVector3 previous{};
        bool have_previous{false};
        bool seen{false};
    };

    std::map<mc::EntityId, Entry> entries_;
    uint64_t next_id_{static_cast<uint64_t>(mc::EntityId::LocalPlayer) + 1};
};

} // namespace sekiro::live
