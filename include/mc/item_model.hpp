#pragma once

// Held items as Minecraft draws them in third person: a flat sprite extruded one pixel deep (a pixel with
// opaque neighbours gets no side wall), placed in the right hand by the vanilla ItemInHandLayer transform and
// the model's display transform. The mesh comes out in the rest pose of the right arm, in the same space as
// the Steve parts (buildPartMesh), so drawing it with the RightArm's matrix makes it follow the swinging arm.

#include "mc/rig.hpp"
#include "mc/types.hpp"

#include <cstdint>

namespace mc::rig {

// Where the item's pixels live: a cell of an RGBA atlas (alpha >= 128 counts as opaque, like the shader's cut-out).
struct ItemSprite {
    const uint8_t* rgba{nullptr};
    int atlas_w{0};
    int atlas_h{0};
    int x{0}, y{0}, w{0}, h{0}; // the sprite's cell inside the atlas, pixels
};

enum class HeldItemStyle {
    Handheld,  // swords, tools: item/handheld, rotation (0,-90,55), translation (0,4,0.5), scale 0.85
    Generated, // plain items: rotation (0,0,0), translation (0,3,1), scale 0.55
    Bow,       // rotation (-80,260,-40), translation (-1,-2,2.5), scale 0.9
};

// False for blocks (the atlas holds isometric icons, not sprites) and for the elytra (worn, not held).
[[nodiscard]] bool isHeldAsFlatSprite(ItemId item);
[[nodiscard]] HeldItemStyle heldItemStyle(ItemId item);

// Empty mesh for an unusable sprite. Vertices are in `basis` host units, UVs address the atlas (0..1) at the
// centre of each source pixel, so point sampling returns the sprite's colours.
RigMesh buildHeldItemMesh(const ItemSprite& sprite, HeldItemStyle style, const HostBasis& basis);

} // namespace mc::rig
