#pragma once

#include <cstdint>
#include <cstddef>

namespace eldenring::roll {

// Virtual key codes:
inline constexpr int kVkShift  = 0x10; // VK_SHIFT
inline constexpr int kVkLShift = 0xA0; // VK_LSHIFT
inline constexpr int kVkRShift = 0xA1; // VK_RSHIFT
inline constexpr int kVkSpace  = 0x20; // VK_SPACE

// DirectInput scan codes:
inline constexpr int kDikLShift = 0x2A; // DIK_LSHIFT
inline constexpr int kDikRShift = 0x36; // DIK_RSHIFT
inline constexpr int kDikSpace  = 0x39; // DIK_SPACE
inline constexpr int kDikF      = 0x21; // DIK_F (Dual Wield / two-handing)
inline constexpr int kDikE      = 0x12; // DIK_E (Inventory / Interaction)

// Default roll key in Minecraft control scheme: Shift is sneak, so Shift roll must be suppressed
inline constexpr int kDefaultRollVk = kVkShift;

struct RollMaskResult {
    int scancodes[4]{};
    size_t count{0};
};

// Computes the scan codes that must be masked from DirectInput to disable vanilla roll
inline RollMaskResult computeRollMask(bool disable_vanilla_roll, bool mc_mode, int roll_vk) {
    RollMaskResult res{};
    if (!disable_vanilla_roll || !mc_mode) return res;

    if (roll_vk == kVkShift || roll_vk == kVkLShift || roll_vk == kVkRShift) {
        res.scancodes[res.count++] = kDikLShift;
        res.scancodes[res.count++] = kDikRShift;
    } else if (roll_vk == kVkSpace) {
        res.scancodes[res.count++] = kDikSpace;
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

// Asserts that an essential gameplay key (e.g. Space for Jump, F for Dual Wield) is NOT masked.
inline bool isEssentialKeyPreserved(const int* masked_keys, size_t count, int scan_code) {
    if (!masked_keys) return true;
    for (size_t i = 0; i < count; ++i) {
        if (masked_keys[i] == scan_code) return false;
    }
    return true;
}

} // namespace eldenring::roll
