#pragma once

// Read-only description of a CSBulletManager::spawn_bullet request (0x1403A2CB0, docs/ELDENRING_REVERSE.md 23, 34): hex dump, the fields
// at the offsets the audit accepted, and a search for a known id anywhere in the body (the reverser's two field tables disagree, so the log
// must show which dword really holds the id). Pure logic, unit-tested without the game.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace eldenring::bullet {

inline constexpr size_t kRequestBytes = 0xB8; // the smallest size the reverser gave; the real size is 0x110 per the constructor, only the first 0xB8 are logged

struct Fields {
    bool valid{false};
    uint64_t owner{0};      // +0x00, the shooter's 64-bit entity handle (ChrIns+0x08)
    uint64_t target{0};     // +0x08, -1 = no lock-on
    uint32_t id_at_1c{0};   // +0x1C, BulletParam id per REVERSE 23 (disputed: see findDword)
    uint32_t flags_at_44{0};
    float right[3]{}, up[3]{}, forward[3]{}, position[3]{}; // the row-major world matrix at +0x50
};

inline Fields decode(const uint8_t* b, size_t n) {
    Fields f;
    if (b == nullptr || n < 0x90) return f;
    std::memcpy(&f.owner, b + 0x00, 8);
    std::memcpy(&f.target, b + 0x08, 8);
    std::memcpy(&f.id_at_1c, b + 0x1C, 4);
    std::memcpy(&f.flags_at_44, b + 0x44, 4);
    std::memcpy(f.right, b + 0x50, 12);
    std::memcpy(f.up, b + 0x60, 12);
    std::memcpy(f.forward, b + 0x70, 12);
    std::memcpy(f.position, b + 0x80, 12);
    f.valid = true;
    return f;
}

// "+0x010: 00 00 .. (16 bytes)", one line per 16 bytes (a short tail is printed as is).
inline std::vector<std::string> hexDump(const uint8_t* b, size_t n) {
    std::vector<std::string> lines;
    for (size_t at = 0; at < n; at += 16) {
        char head[16];
        std::snprintf(head, sizeof(head), "+0x%03zX: ", at);
        std::string line = head;
        for (size_t i = 0; i < 16 && at + i < n; ++i) {
            char byte[4];
            std::snprintf(byte, sizeof(byte), i == 0 ? "%02X" : " %02X", b[at + i]);
            line += byte;
        }
        lines.push_back(line);
    }
    return lines;
}

// Every 4-byte aligned offset whose dword equals `value`.
inline std::vector<int> findDword(const uint8_t* b, size_t n, uint32_t value) {
    std::vector<int> out;
    for (size_t at = 0; at + 4 <= n; at += 4) {
        uint32_t v;
        std::memcpy(&v, b + at, 4);
        if (v == value) out.push_back(static_cast<int>(at));
    }
    return out;
}

// A probe that logs without limit would flood the log (a bow fires often): only the first `limit` requests are written.
class LogBudget {
public:
    explicit LogBudget(unsigned limit) : left_(limit) {}
    bool take() {
        if (left_ == 0) return false;
        --left_;
        return true;
    }

private:
    unsigned left_;
};

} // namespace eldenring::bullet
