#pragma once

// Read-only search for a 32-bit value (the entity handle spawn_bullet wants in request+0x08) in a few structures and one pointer level below
// them, so the log says where the game keeps it. Pure logic on the injected IMemoryReader; unit-tested with fake memory.

#include "eldenring_live.hpp"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace eldenring::handlescan {

struct Root {
    std::string name;
    uintptr_t address{0};
    size_t bytes{0};
};

namespace detail {
// Reads in 0x100-byte pieces so that one unreadable page does not hide the rest; unreadable pieces stay zero.
inline std::vector<uint8_t> readTolerant(const live::IMemoryReader& r, uintptr_t address, size_t bytes) {
    std::vector<uint8_t> out(bytes, 0);
    for (size_t at = 0; at < bytes; at += 0x100) {
        const size_t n = bytes - at < 0x100 ? bytes - at : 0x100;
        r.read(address + at, out.data() + at, n);
    }
    return out;
}
inline bool looksLikeUserPointer(uint64_t p) { return p >= 0x10000 && p < 0x00007FFFFFFF0000ull && (p & 7) == 0; }
inline std::string hex(size_t v) {
    char b[24];
    std::snprintf(b, sizeof(b), "0x%zX", v);
    return b;
}
} // namespace detail

// Hits as "name+0xOFF" (in the root) and "[name+0xOFF]+0xOFF2" (one pointer level down), at most `max_hits`.
inline std::vector<std::string> findValue(const live::IMemoryReader& reader, const std::vector<Root>& roots, uint32_t value, size_t child_bytes = 0x200,
                                         size_t max_hits = 32) {
    std::vector<std::string> hits;
    std::set<uintptr_t> scanned_children;
    for (const Root& root : roots) {
        if (root.address == 0 || root.bytes == 0) continue;
        const std::vector<uint8_t> body = detail::readTolerant(reader, root.address, root.bytes);
        for (size_t at = 0; at + 4 <= body.size(); at += 4) {
            uint32_t v;
            std::memcpy(&v, body.data() + at, 4);
            if (v == value && hits.size() < max_hits) hits.push_back(root.name + "+" + detail::hex(at));
        }
        for (size_t at = 0; at + 8 <= body.size(); at += 8) {
            uint64_t p;
            std::memcpy(&p, body.data() + at, 8);
            if (!detail::looksLikeUserPointer(p) || !scanned_children.insert(static_cast<uintptr_t>(p)).second) continue;
            uint8_t probe;
            if (!reader.read(static_cast<uintptr_t>(p), &probe, 1)) continue;
            const std::vector<uint8_t> child = detail::readTolerant(reader, static_cast<uintptr_t>(p), child_bytes);
            for (size_t c = 0; c + 4 <= child.size(); c += 4) {
                uint32_t v;
                std::memcpy(&v, child.data() + c, 4);
                if (v == value && hits.size() < max_hits) hits.push_back("[" + root.name + "+" + detail::hex(at) + "]+" + detail::hex(c));
            }
        }
    }
    return hits;
}

} // namespace eldenring::handlescan
