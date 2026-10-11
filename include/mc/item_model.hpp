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

enum class Hand {
    Right,
    Left
};

enum class HeldItemStyle {
    Handheld,  // swords, tools: item/handheld, rotation (0,-90,55), translation (0,4,0.5), scale 0.85
    Generated, // plain items: rotation (0,0,0), translation (0,3,1), scale 0.55
    Bow,       // rotation (-80,260,-40), translation (-1,-2,2.5), scale 0.9
    Shield     // shield: attached to the forearm, scale 0.95
};

// False for blocks (the atlas holds isometric icons, not sprites) and for the elytra (worn, not held).
[[nodiscard]] bool isHeldAsFlatSprite(ItemId item);
[[nodiscard]] HeldItemStyle heldItemStyle(ItemId item);

// Empty mesh for an unusable sprite. Vertices are in `basis` host units, UVs address the atlas (0..1) at the
// centre of each source pixel, so point sampling returns the sprite's colours.
RigMesh buildHeldItemMesh(const ItemSprite& sprite, HeldItemStyle style, const HostBasis& basis, Hand hand = Hand::Right);

// A block held in the hand in third person (block/block display: rotation (75, 45, 0), translation (0, 2.5, 0), scale 0.375): a
// textured cube in the same space as buildHeldItemMesh. The faces are cells of an atlas, given as UV rectangles.
struct UvRect {
    float u0{0.f}, v0{0.f}, u1{1.f}, v1{1.f};
};
RigMesh buildHeldBlockMesh(const UvRect& top, const UvRect& side, const UvRect& bottom, const HostBasis& basis);

// The sprite as a flat, one pixel thick extrusion centred on the origin, for the first-person view: x right, y up, z towards the
// viewer, 1 unit = 1 block (16 sprite pixels), so the mesh spans -0.5..0.5 and is 1/16 thick. UVs address the atlas at the centre
// of each source pixel. Empty for an unusable sprite.
RigMesh buildFlatItemMesh(const ItemSprite& sprite);

// The vanilla shield (ShieldModel): a plate box 12x22x1 (texOffs (0, 0)) and a handle box 2x6x6 (texOffs (26, 0)) mapped onto the 64x64
// entity texture like the game does, so the front has the rivets, the back is plain wood and the handle sits behind the plate. `sheet` is
// the cell of the atlas that holds the whole 64x64 texture. Third person: held on the forearm of `hand`, in the same space as
// buildHeldItemMesh. The plate faces forward, the handle reaches back to the arm.
inline constexpr float kShieldModelScale = 0.7f; // the 22 px plate is about as tall as the old 16 px sprite was
RigMesh buildShieldMesh(const UvRect& sheet, const HostBasis& basis, Hand hand);

// First person: the same model centred on the plate, x right, y up, z towards the viewer, 1 unit = 1 block, the plate (22 px) one block tall.
// The front (rivets) faces the viewer and the handle is behind it.
RigMesh buildShieldFirstPersonMesh(const UvRect& sheet);

} // namespace mc::rig
