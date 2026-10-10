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


// ---- firing a bolt of our own from a request the game itself wrote ---------------------------------------------------------------------
inline constexpr size_t kFullRequestBytes = 0x110; // the constructor's size (0x14038C580); the logger only dumps the first kRequestBytes

struct FireParams {
    uint64_t owner{0};   // the shooter's entity handle (player ChrIns+0x08)
    uint64_t target{0xFFFFFFFFFFFFFFFFull};
    uint32_t flags{0x08}; // +0x44: bit 3 must be set (spawn_bullet returns early otherwise), bit 1 must be clear
    float right[3]{}, up[3]{}, forward[3]{}, position[3]{}; // row-major world matrix at +0x50
};

// A copy of a real request (from the logger) with the fields we own replaced; everything else is what the game wrote. +0xB0 (a pointer to a
// stack object in the real request) is zeroed: copying a dead stack address would be worse than none. Empty when the template is unusable.
inline std::vector<uint8_t> buildFireRequest(const uint8_t* real, size_t n, const FireParams& p) {
    if (real == nullptr || n < 0x90) return {};
    std::vector<uint8_t> out(kFullRequestBytes, 0);
    std::memcpy(out.data(), real, n < kFullRequestBytes ? n : kFullRequestBytes);
    std::memcpy(out.data() + 0x00, &p.owner, 8);
    std::memcpy(out.data() + 0x08, &p.target, 8);
    std::memcpy(out.data() + 0x44, &p.flags, 4);
    std::memcpy(out.data() + 0x50, p.right, 12);
    std::memcpy(out.data() + 0x60, p.up, 12);
    std::memcpy(out.data() + 0x70, p.forward, 12);
    std::memcpy(out.data() + 0x80, p.position, 12);
    const float one = 1.f; // w of the translation row, as in the real request
    std::memcpy(out.data() + 0x8C, &one, 4);
    std::memset(out.data() + 0xB0, 0, 8);
    return out;
}


// Which of the two things the game wants (the audit of the first bolts: free aim, target -1 with flags 0x08, was refused; the real shot's target
// handle with flags 0x09 was accepted): one variant per F3 press, so the log says which single change is enough. None of them is guided
// (bit 0) while targeting the shooter.
struct Variant {
    std::string name;
    uint64_t target{0};
    uint32_t flags{0};
};

inline unsigned variantCount() { return 4; }

inline Variant variant(unsigned index, uint64_t real_target, uint32_t real_flags, uint64_t own_handle) {
    (void)real_flags;
    switch (index % variantCount()) {
        case 0: return {"real target handle, bit 0 clear", real_target, 0x08};
        case 1: return {"no target (-1), bit 0 set", 0xFFFFFFFFFFFFFFFFull, 0x09};
        case 2: return {"the shooter's own handle, bit 0 clear", own_handle, 0x08};
        default: return {"target 0, bit 0 clear", 0, 0x08};
    }
}

inline void muzzle(const float eye[3], const float forward[3], float distance, float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = eye[i] + forward[i] * distance;
}

inline bool spawnFailed(uint32_t handle) { return handle == 0xFFFFFFFFu; }

} // namespace eldenring::bullet
