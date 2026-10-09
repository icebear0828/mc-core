#pragma once

// Read-only world layer for Elden Ring: team relations and enemy enumeration. Nothing here writes to the game
// and nothing calls game code. Memory goes through IMemoryReader (see eldenring_live.hpp), so it is unit-tested
// against fake memory. Offsets are those of eldenring.exe 2.7.1.0 (docs/ELDENRING_REVERSE.md).

#include "eldenring_live.hpp"
#include "eldenring_rtti.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace eldenring::live {

namespace layout {

inline constexpr uintptr_t kNpcIdInChrIns = 0x64;    // A
inline constexpr uintptr_t kTeamTypeInChrIns = 0x6C; // A (u8; +0x68 is chr_type, not a team)

inline constexpr uintptr_t kChrSetPointerArrayInWorldChrMan = 0x1DED8; // A: 196 pointers
inline constexpr uint32_t kChrSetPointerCount = 196;
inline constexpr uintptr_t kChrSetCapacity = 0x10; // u32
inline constexpr uintptr_t kChrSetEntries = 0x18;  // pointer to 16-byte {ChrIns*, handle} entries (B+)
inline constexpr uint32_t kMaxChrSetCapacity = 4096; // set 115 is 1500; anything above is a wrong read

inline constexpr uint32_t kEnemyInsVtableRva = 0x2A47090;  // A
inline constexpr uint32_t kPlayerInsVtableRva = 0x2A7FBB0; // A: humanoid NPCs share it (B)

inline constexpr uintptr_t kPhysicsModuleSlot = 0x0D;     // A
inline constexpr uint32_t kPhysicsModuleVtableRva = 0x2A3C890; // A
inline constexpr uintptr_t kPhysicsOrientation = 0x50;   // A: float x,y,z,w, unit length
inline constexpr uintptr_t kIsFallingInPhysics = 0x1D0;      // B (fromsoftware-rs layout, not yet checked live): bool
inline constexpr uintptr_t kPhysicsPosition = 0x70;       // A: float x,y,z; the only basis for relative positions

// c1000 map anchors (sites of grace and the like). Filter by npc_id, never by "team 0".
inline constexpr int32_t kGraceNpcId = 1000;

// Team relation matrix (B: static analysis of CheckTeamRelation 0x14051B5D0, 79*79 pointers to 4 singletons).
inline constexpr uint32_t kTeamCount = 79;
inline constexpr uint32_t kTeamMatrixRva = 0x3B283F0;
inline constexpr uint32_t kRelHostileRva = 0x3B1C0C0, kRelHostileVtableRva = 0x2A4F888;
inline constexpr uint32_t kRelFriendRva = 0x3B1C0B8, kRelFriendVtableRva = 0x2A4F878;
inline constexpr uint32_t kRelNeutralRva = 0x3B1C0D0, kRelNeutralVtableRva = 0x2A4F868;
inline constexpr uint32_t kRelAllRva = 0x3B1C0C8, kRelAllVtableRva = 0x2A4F898;

// A player cannot move farther than this between the two reads of one tick; more means the floating origin
// re-based (Havok space jumps in multiples of 8 m) or the read was torn.
inline constexpr float kMaxPlayerMoveWithinTick = 5.0f;

} // namespace layout

enum class TeamRelation { Unknown, Neutral, Friend, Hostile, All };

// Relation of `from` toward `to` in the engine's matrix. Unknown unless the matrix entry points at one of the four
// relation objects AND that object carries the expected vtable (guards against a build where the layout moved).
inline TeamRelation readTeamRelation(const IMemoryReader& reader, uintptr_t image_base, uint32_t from, uint32_t to) {
    if (from >= layout::kTeamCount || to >= layout::kTeamCount) return TeamRelation::Unknown;
    uint64_t entry = 0;
    const uintptr_t at = image_base + layout::kTeamMatrixRva + (static_cast<uintptr_t>(from) * layout::kTeamCount + to) * 8;
    if (!reader.read(at, &entry, sizeof(entry))) return TeamRelation::Unknown;
    struct Kind {
        uint32_t rva, vtable_rva;
        TeamRelation relation;
    };
    static constexpr Kind kinds[] = {
        {layout::kRelHostileRva, layout::kRelHostileVtableRva, TeamRelation::Hostile},
        {layout::kRelFriendRva, layout::kRelFriendVtableRva, TeamRelation::Friend},
        {layout::kRelNeutralRva, layout::kRelNeutralVtableRva, TeamRelation::Neutral},
        {layout::kRelAllRva, layout::kRelAllVtableRva, TeamRelation::All},
    };
    for (const auto& k : kinds) {
        if (entry != image_base + k.rva) continue;
        uint64_t vtable = 0;
        if (!reader.read(image_base + k.rva, &vtable, sizeof(vtable)) || vtable != image_base + k.vtable_rva) {
            return TeamRelation::Unknown;
        }
        return k.relation;
    }
    return TeamRelation::Unknown;
}

struct EnemyInfo {
    uintptr_t chr{0}; // host pointer: valid for this tick only; never expose it as an EntityId
    uint64_t handle{0};
    int32_t npc_id{0};
    uint8_t team{0};
    int32_t hp{0};
    int32_t max_hp{0};
    float rel_x{0}, rel_y{0}, rel_z{0}; // metres, Havok axes (x right, y up, z forward), enemy minus player
    bool hostile{false};                // only when the matrix says player -> team is HOSTILE
};

namespace detail {

// The character's CSChrPhysicsModule (class and owner checked), or 0.
inline uintptr_t readPhysicsModule(const IMemoryReader& reader, uintptr_t image_base, uintptr_t chr) {
    uint64_t container = 0, module = 0, owner = 0;
    if (!reader.read(chr + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0) return 0;
    if (!reader.read(static_cast<uintptr_t>(container) + layout::kPhysicsModuleSlot * sizeof(uint64_t), &module, sizeof(module)) ||
        module == 0) {
        return 0;
    }
    const auto m = static_cast<uintptr_t>(module);
    if (!objectIsClass(reader, image_base, m, layout::kPhysicsModuleVtableRva, ".?AVCSChrPhysicsModule@CS@@")) return 0;
    if (!reader.read(m + layout::kOwnerInDataModule, &owner, sizeof(owner)) || owner != chr) return 0;
    return m;
}

inline bool readPhysicsPosition(const IMemoryReader& reader, uintptr_t image_base, uintptr_t chr, float out[3]) {
    const uintptr_t m = readPhysicsModule(reader, image_base, chr);
    if (m == 0) return false;
    float p[3];
    if (!reader.read(m + layout::kPhysicsPosition, p, sizeof(p))) return false;
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) return false;
    std::memcpy(out, p, sizeof(p));
    return true;
}

// PhysicsModule+0x1D0 `is_falling`. False when the module cannot be read.
inline bool readIsFalling(const IMemoryReader& reader, uintptr_t image_base, uintptr_t chr) {
    const uintptr_t m = readPhysicsModule(reader, image_base, chr);
    uint8_t v = 0;
    return m != 0 && reader.read(m + layout::kIsFallingInPhysics, &v, sizeof(v)) && v != 0;
}

// Orientation quaternion (x, y, z, w) at PhysicsModule+0x50 (A: unit length, yaw agrees with BlockPosition.yaw).
inline bool readPhysicsOrientation(const IMemoryReader& reader, uintptr_t image_base, uintptr_t chr, float out[4]) {
    const uintptr_t m = readPhysicsModule(reader, image_base, chr);
    if (m == 0) return false;
    float q[4];
    if (!reader.read(m + layout::kPhysicsOrientation, q, sizeof(q))) return false;
    for (float v : q) {
        if (!std::isfinite(v)) return false;
    }
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(len > 0.9f && len < 1.1f)) return false;
    std::memcpy(out, q, sizeof(q));
    return true;
}

inline bool isCharacterClass(const IMemoryReader& reader, uintptr_t image_base, uintptr_t chr) {
    return objectIsClass(reader, image_base, chr, layout::kEnemyInsVtableRva, ".?AVEnemyIns@CS@@") ||
           objectIsClass(reader, image_base, chr, layout::kPlayerInsVtableRva, ".?AVPlayerIns@CS@@");
}

} // namespace detail

// Lists live characters around the player (enemies and humanoid NPCs alike; the caller decides by `hostile`).
// Returns false, with `out` empty, when not in a world or when the player's position moved more than one tick
// allows during the read (origin re-base / torn read): the caller keeps last tick's data and tries again.
// Excluded: the main player, c1000 anchors (npc_id 1000), dead characters (hp 0), unreadable or foreign-class
// entries, duplicates reachable from several sets. At most `max` entries.
inline bool enumerateEnemies(const IMemoryReader& reader, uintptr_t image_base, std::vector<EnemyInfo>& out, size_t max) {
    out.clear();
    uint64_t world = 0, player = 0;
    if (!reader.read(image_base + layout::kWorldChrManGlobalRva, &world, sizeof(world)) || world == 0) return false;
    if (!reader.read(static_cast<uintptr_t>(world) + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0) {
        return false;
    }
    const auto player_chr = static_cast<uintptr_t>(player);
    float p0[3];
    if (!detail::readPhysicsPosition(reader, image_base, player_chr, p0)) return false;
    uint8_t player_team = 0;
    if (!reader.read(player_chr + layout::kTeamTypeInChrIns, &player_team, sizeof(player_team))) return false;

    std::vector<EnemyInfo> found;
    for (uint32_t s = 0; s < layout::kChrSetPointerCount && found.size() < max; ++s) {
        uint64_t set = 0;
        if (!reader.read(static_cast<uintptr_t>(world) + layout::kChrSetPointerArrayInWorldChrMan + s * 8, &set, sizeof(set)) ||
            set == 0) {
            continue;
        }
        uint32_t capacity = 0;
        uint64_t entries = 0;
        if (!reader.read(static_cast<uintptr_t>(set) + layout::kChrSetCapacity, &capacity, sizeof(capacity))) continue;
        if (!reader.read(static_cast<uintptr_t>(set) + layout::kChrSetEntries, &entries, sizeof(entries)) || entries == 0) continue;
        if (capacity == 0 || capacity > layout::kMaxChrSetCapacity) continue;
        for (uint32_t i = 0; i < capacity && found.size() < max; ++i) {
            uint64_t pair[2];
            if (!reader.read(static_cast<uintptr_t>(entries) + static_cast<uintptr_t>(i) * 16, pair, sizeof(pair))) break;
            const auto chr = static_cast<uintptr_t>(pair[0]);
            if (chr == 0 || chr == player_chr) continue;
            if (std::any_of(found.begin(), found.end(), [&](const EnemyInfo& e) { return e.chr == chr; })) continue;
            if (!detail::isCharacterClass(reader, image_base, chr)) continue;
            EnemyInfo e;
            e.chr = chr;
            e.handle = pair[1];
            if (!reader.read(chr + layout::kNpcIdInChrIns, &e.npc_id, sizeof(e.npc_id))) continue;
            if (e.npc_id == layout::kGraceNpcId) continue;
            if (!reader.read(chr + layout::kTeamTypeInChrIns, &e.team, sizeof(e.team))) continue;
            Vitals v;
            if (!readVitals(reader, image_base, chr, v) || v.hp <= 0) continue;
            e.hp = v.hp;
            e.max_hp = v.max_hp;
            float pos[3];
            if (!detail::readPhysicsPosition(reader, image_base, chr, pos)) continue;
            e.rel_x = pos[0] - p0[0];
            e.rel_y = pos[1] - p0[1];
            e.rel_z = pos[2] - p0[2];
            e.hostile = readTeamRelation(reader, image_base, player_team, e.team) == TeamRelation::Hostile;
            found.push_back(e);
        }
    }

    float p1[3];
    if (!detail::readPhysicsPosition(reader, image_base, player_chr, p1)) return false;
    const float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > layout::kMaxPlayerMoveWithinTick) return false;
    out = std::move(found);
    return true;
}

} // namespace eldenring::live
