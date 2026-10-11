#pragma once

// Which of the depth textures the game creates is the scene's (the one the Steve figure, the blocks and the shadow are tested against).
// The game also creates R32G8X24_TYPELESS depth textures of other sizes (1024x512 shadow-like passes, created after a load or a respawn); taking the
// newest one made every figure read as hidden behind the scene (REVERSE 43, live: Steve vanished after a death).

#include <cstdint>

namespace eldenring::depth {

// The scene depth is as wide as the back buffer (the engine may round the height up). With no known back buffer yet nothing can be excluded.
inline bool isSceneDepth(uint64_t depth_width, uint32_t depth_height, uint32_t back_width, uint32_t back_height) {
    (void)depth_height;
    (void)back_height;
    if (back_width == 0) return true;
    return depth_width == back_width;
}

} // namespace eldenring::depth
