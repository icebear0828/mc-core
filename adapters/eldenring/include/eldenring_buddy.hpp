#pragma once

// Read-only view of CSBuddyMan, the game's spirit-ash summon manager. Nothing here writes to the game.
//
// Evidence (docs/ELDENRING_REVERSE.md 29, bytes checked against eldenring.exe 2.7.1.0): WorldChrMan+0x1E538 holds the
// CSBuddyMan (stored at 0x14050BFE5). +0x20 is the pending-request slot (-1 = none; tested at 0x1404B8952 and reset to -1 at
// 0x1404B89CF), +0x24 the active one (copied from +0x20 at 0x1404B89CB), +0x3C the tablet id (read at 0x1404BBE29, looked
// up by 0x140D28630), +0x88 a busy count (spawn is skipped while it is positive, 0x1404B89B5). What each value is while
// playing was observed live (mc_er.log, 2026-10-09): idle = request -1, active -1, tablet 0, +0x88 = -1. Using an ash writes
// request=232000 and tablet=1042360100 (a map entity id) in the same instant; the next frame (16 ms) moves request to active
// and clears request; the tablet goes back to 0 after ~13 s; active is NOT cleared on dismissal. The request is
// ash_id * 100 + upgrade level (0x1404BBEE2 splits it by 100): 232000 = ash 2320, level 0.

#include "eldenring_live.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <optional>
#include <algorithm>
#include <string>
#include <vector>

namespace eldenring::buddy {

namespace layout {
inline constexpr uintptr_t kBuddyManInWorldChrMan = 0x1E538; // A
inline constexpr uintptr_t kRequestId = 0x20;                // A (int32)
inline constexpr uintptr_t kActiveId = 0x24;                 // A (int32)
inline constexpr uintptr_t kTabletId = 0x3C;                 // A (int32)
inline constexpr uintptr_t kSummonedFlag = 0x80;             // B (int32): 1 once the world is up, 0 when leaving it (NOT 'ash summoned')
inline constexpr uintptr_t kBusyCount = 0x88;                // A (int32): observed -1 in both states
inline constexpr uintptr_t kSpawnPos = 0xA0;                 // B (4 floats): where the units appear; added to each unit's offset at 0x1404BC159
inline constexpr uintptr_t kSpawnYaw = 0xB0;                 // B (float): heading the offsets are rotated by (0x1404BC09E)
inline constexpr size_t kRawBytes = 0xC0;                    // dumped on every change, to find the neighbours' meaning
} // namespace layout

struct Snapshot {
    bool valid{false};
    uintptr_t buddy_man{0};
    uint32_t vtable_rva{0}; // first qword minus the image base, 0 when it is not inside a plausible image range
    int32_t request{-1};
    int32_t active{-1};
    int32_t tablet{-1};
    int32_t busy{0};
    int32_t summoned{0};
    float spawn_pos[4]{};
    float spawn_yaw{0.f};
    std::array<uint32_t, layout::kRawBytes / 4> raw{};

    [[nodiscard]] bool sameFields(const Snapshot& o) const {
        return valid == o.valid && request == o.request && active == o.active && tablet == o.tablet && busy == o.busy &&
               summoned == o.summoned;
    }
};

// Samples the manager through the WorldChrMan global. `valid` is false when not in a world or any read fails.
inline Snapshot sample(const live::IMemoryReader& reader, uintptr_t image_base) {
    Snapshot s;
    uint64_t world = 0, man = 0;
    if (!reader.read(image_base + live::layout::kWorldChrManGlobalRva, &world, sizeof(world)) || world == 0) return s;
    if (!reader.read(static_cast<uintptr_t>(world) + layout::kBuddyManInWorldChrMan, &man, sizeof(man)) || man == 0) return s;
    const auto base = static_cast<uintptr_t>(man);
    if (!reader.read(base, s.raw.data(), layout::kRawBytes)) return s;
    auto field = [&](uintptr_t off) { return static_cast<int32_t>(s.raw[off / 4]); };
    s.buddy_man = base;
    s.request = field(layout::kRequestId);
    s.active = field(layout::kActiveId);
    s.tablet = field(layout::kTabletId);
    s.busy = field(layout::kBusyCount);
    s.summoned = field(layout::kSummonedFlag);
    std::memcpy(s.spawn_pos, &s.raw[layout::kSpawnPos / 4], sizeof(s.spawn_pos));
    std::memcpy(&s.spawn_yaw, &s.raw[layout::kSpawnYaw / 4], sizeof(s.spawn_yaw));
    const uint64_t vt = (static_cast<uint64_t>(s.raw[1]) << 32) | s.raw[0];
    if (vt > image_base && vt - image_base < 0x10000000ull) s.vtable_rva = static_cast<uint32_t>(vt - image_base);
    s.valid = true;
    return s;
}

// Logs a line each time one of the tracked fields changes. Feed it every sample; it returns the text to log (empty = nothing).
class Monitor {
public:
    std::string update(const Snapshot& now) {
        if (!have_prev_ || !now.sameFields(prev_)) {
            std::string text = describe(now, !have_prev_ ? nullptr : &prev_);
            prev_ = now;
            have_prev_ = true;
            return text;
        }
        return {};
    }

private:
    static std::string describe(const Snapshot& s, const Snapshot* prev) {
        char buf[160];
        std::string out;
        if (!s.valid) return prev == nullptr ? "buddy: not available (no world or unreadable)" : "buddy: became unavailable";
        std::snprintf(buf, sizeof(buf), "buddy: man=%llx vtbl=+%X request=%d active=%d tablet=%d busy=%d summoned=%d%s",
                      static_cast<unsigned long long>(s.buddy_man), s.vtable_rva, s.request, s.active, s.tablet, s.busy, s.summoned,
                      prev == nullptr ? " (first)" : "");
        out = buf;
        std::snprintf(buf, sizeof(buf), "\nbuddy: spawn=(%.2f %.2f %.2f %.2f) yaw=%.2f", static_cast<double>(s.spawn_pos[0]),
                      static_cast<double>(s.spawn_pos[1]), static_cast<double>(s.spawn_pos[2]), static_cast<double>(s.spawn_pos[3]),
                      static_cast<double>(s.spawn_yaw));
        out += buf;
        out += "\nbuddy: raw";
        for (size_t i = 0; i < s.raw.size(); ++i) {
            if (i % 8 == 0) {
                std::snprintf(buf, sizeof(buf), "\n  +%02zX:", i * 4);
                out += buf;
            }
            std::snprintf(buf, sizeof(buf), " %08X", s.raw[i]);
            out += buf;
        }
        return out;
    }

    bool have_prev_{false};
    Snapshot prev_{};
};

// ---- the write experiment (mc_er_summon.txt + F2) -----------------------------------------------------------------------
// Mirrors what the item code was observed to do: the spawn point, the tablet id and the request appear together, the frame
// update consumes the request. The request goes last so everything else is already there when it becomes visible. +0x38 and
// +0x44 are set by the game itself and are NOT written.

// Where the units appear: the item code was observed to write the player's position plus 3 m straight ahead (in the player's
// real facing, which is opposite to the orientation quaternion's own forward) and the player's heading, in the same instant as
// the request. Observed: player (-7.57, 7.23, 7.61) heading -2.51 -> (-5.79, 7.15, 10.03, 1.00) yaw -2.51.
struct SpawnPoint {
    float pos[4]{0.f, 0.f, 0.f, 1.f};
    float yaw{0.f};
};

inline SpawnPoint spawnPointAhead(const float player_pos[3], float heading, float distance = 3.0f) {
    SpawnPoint p;
    p.pos[0] = player_pos[0] - std::sin(heading) * distance;
    p.pos[1] = player_pos[1];
    p.pos[2] = player_pos[2] - std::cos(heading) * distance;
    p.pos[3] = 1.0f;
    p.yaw = heading;
    return p;
}

struct SummonPlan {
    uintptr_t spawn_address{0}; // 4 floats
    uintptr_t yaw_address{0};
    uintptr_t tablet_address{0};
    uintptr_t request_address{0};
    SpawnPoint spawn{};
    int32_t tablet{0};
    int32_t request{-1};
};

// Why a summon cannot be requested right now ("" = it can).
inline const char* refusalReason(const Snapshot& s) {
    if (!s.valid) return "buddy manager not readable (not in a world)";
    if (s.summoned == 0) return "world is not up yet (+0x80 == 0)";
    if (s.request != -1) return "a request is already pending";
    if (s.busy > 0) return "manager is busy (+0x88 > 0)";
    return "";
}

inline std::optional<SummonPlan> planSummon(const Snapshot& s, int32_t request, int32_t tablet, const SpawnPoint& spawn) {
    if (refusalReason(s)[0] != '\0') return std::nullopt;
    if (request <= 0 || tablet <= 0) return std::nullopt;
    for (float v : spawn.pos) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    if (!std::isfinite(spawn.yaw)) return std::nullopt;
    return SummonPlan{s.buddy_man + layout::kSpawnPos, s.buddy_man + layout::kSpawnYaw, s.buddy_man + layout::kTabletId,
                      s.buddy_man + layout::kRequestId, spawn, tablet, request};
}

// Indices into `after` of the entities that were not in `before` (what appeared after a summon).
inline std::vector<size_t> newEntities(const std::vector<uintptr_t>& before, const std::vector<uintptr_t>& after) {
    std::vector<size_t> out;
    for (size_t i = 0; i < after.size(); ++i) {
        if (std::find(before.begin(), before.end(), after[i]) == before.end()) out.push_back(i);
    }
    return out;
}

} // namespace eldenring::buddy
