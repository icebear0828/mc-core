#pragma once

// Pixel conversions for the F4 screenshot (the back buffer of the game is DXGI_FORMAT_R10G10B10A2_UNORM, format 24).

#include <cstdint>

namespace eldenring::pixel {

// R in bits 0-9, G in 10-19, B in 20-29, A in 30-31; `rgb` receives the three channels as 8 bits (rounded, 255 / 1023).
inline void unpackR10G10B10A2(uint32_t v, uint8_t rgb[3]) {
    for (int c = 0; c < 3; ++c) {
        const uint32_t ten = (v >> (10 * c)) & 0x3FFu;
        rgb[c] = static_cast<uint8_t>((ten * 255u + 511u) / 1023u);
    }
}

} // namespace eldenring::pixel
