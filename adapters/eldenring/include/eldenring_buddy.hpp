#pragma once

// Read-only view of CSBuddyMan, the game's spirit-ash summon manager. Nothing here writes to the game.
//
// Evidence (docs/ELDENRING_REVERSE.md 29, bytes checked against eldenring.exe 2.7.1.0): WorldChrMan+0x1E538 holds the
// CSBuddyMan (stored at 0x14050BFE5). +0x20 is the pending-request slot (-1 = none; tested at 0x1404B8952 and reset to -1 at
// 0x1404B89CF), +0x24 the active one (copied from +0x20 at 0x1404B89CB), +0x3C the tablet id (read at 0x1404BBE29, looked
// up by 0x140D28630), +0x88 a busy count (spawn is skipped while it is positive, 0x1404B89B5). What each value is while
// playing was observed live: idle = request -1, active -1, tablet 0, +0x80 = 0, +0x88 = -1; after using an ash active =
// 232000 (not the 21200000 an external list claimed), tablet = a ten digit map entity id (1042360100), +0x80 = 1.

#include "eldenring_live.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace eldenring::buddy {

namespace layout {
inline constexpr uintptr_t kBuddyManInWorldChrMan = 0x1E538; // A
inline constexpr uintptr_t kRequestId = 0x20;                // A (int32)
inline constexpr uintptr_t kActiveId = 0x24;                 // A (int32)
inline constexpr uintptr_t kTabletId = 0x3C;                 // A (int32)
inline constexpr uintptr_t kSummonedFlag = 0x80;             // B (int32): observed 0 idle, 1 while a ash is summoned
inline constexpr uintptr_t kBusyCount = 0x88;                // A (int32): observed -1 in both states
inline constexpr size_t kRawBytes = 0x98;                    // dumped on every change, to find the neighbours' meaning
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

} // namespace eldenring::buddy
