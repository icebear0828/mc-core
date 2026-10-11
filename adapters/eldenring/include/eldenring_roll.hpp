#pragma once

#include <cstdint>
#include <cstddef>

namespace eldenring::roll {

// Default virtual key and DirectInput scan codes for roll in vanilla Elden Ring
inline constexpr int kDefaultRollVk = 0x20;  // VK_SPACE
inline constexpr int kDefaultRollDik = 0x39; // DIK_SPACE

// Result container for scan codes to mask for roll
struct RollMaskResult {
    int scancodes[4]{};
    size_t count{0};
};

// Computes the scan codes that must be masked from the game to disable vanilla roll.
inline RollMaskResult computeRollMask(bool disable_vanilla_roll, bool mc_mode, int roll_vk) {
    RollMaskResult res{};
    if (!disable_vanilla_roll || !mc_mode) return res;

    if (roll_vk == kDefaultRollVk) {
        res.scancodes[res.count++] = kDefaultRollDik;
    } else if (roll_vk > 0) {
        res.scancodes[res.count++] = roll_vk;
    }
    return res;
}

// Appends new keys to the masked buffer, skipping duplicates and non-positive codes.
inline size_t mergeMaskedKeys(int* dest, size_t max_dest, size_t current_count, const int* new_keys, size_t new_count) {
    if (!dest || max_dest == 0) return 0;
    size_t count = current_count > max_dest ? max_dest : current_count;

    for (size_t i = 0; i < new_count; ++i) {
        const int key = new_keys[i];
        if (key <= 0) continue;
        bool exists = false;
        for (size_t j = 0; j < count; ++j) {
            if (dest[j] == key) {
                exists = true;
                break;
            }
        }
        if (!exists && count < max_dest) {
            dest[count++] = key;
        }
    }
    return count;
}

} // namespace eldenring::roll
