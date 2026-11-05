#pragma once

// A backend-neutral seam for drawing the 12-part Steve rig. The core describes a frame (camera,
// part matrices, what the hands hold, how the host's depth buffer works) and a host adapter turns
// those requests into its own draw calls. Nothing here includes a graphics API: every host object
// (device, command list, texture, descriptor heap) crosses the seam as an opaque void* and every
// GPU handle as a uintptr_t, and the adapter casts them back to its own types.
//
// Elden Ring implements it in adapters/eldenring/src/render/er_rig_renderer.hpp by forwarding to
// SteveRenderer; a second port (e.g. RE4R) only has to provide another implementation.

#include "mc/hand_items.hpp"
#include "mc/rig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mc::rig {

using HostHandle = void*;   // an adapter-owned object: device, command list, texture, heap
using GpuHandle = uintptr_t; // an adapter-owned GPU/CPU descriptor value

inline constexpr size_t kRigPartCount = static_cast<size_t>(mc::StevePart::Count);
using PartMatrices = std::array<Mat4, kRigPartCount>;

// One third-person draw. `part_world` must point at kRigPartCount matrices (mc::rig::partMatrix or
// an adapter's posedMatrices); a null pointer means there is nothing to draw.
struct RigDraw {
    HostHandle command_list{nullptr};
    HostHandle descriptor_heap{nullptr}; // the heap that holds the scene-depth SRV at depth_table
    GpuHandle depth_table{0};            // descriptor table (scene depth, skin)
    GpuHandle render_target{0};          // the back buffer view to draw into
    unsigned width{0};
    unsigned height{0};
    Mat4 view_projection{};
    const PartMatrices* part_world{nullptr};
    bool keep_depth{false}; // the placed blocks were drawn just before: keep the figure's depth buffer
};

// One first-person draw: the bare arm (with its sleeve) and the held item, in the host's view-model
// space. The caller binds the depth state it wants; the seam only describes the geometry.
struct RigFirstPersonDraw {
    HostHandle command_list{nullptr};
    HostHandle descriptor_heap{nullptr};
    GpuHandle skin_table{0};  // descriptor table (scene depth, skin)
    GpuHandle atlas_table{0}; // descriptor table (any, item atlas)
    GpuHandle render_target{0};
    unsigned width{0};
    unsigned height{0};
    Mat4 projection{};
    Mat4 arm_world{};
    Mat4 item_world{};
    uint16_t item{0}; // the sprite to draw (mc::ItemId); 0 draws the bare arm
};

class IRigRenderer {
public:
    virtual ~IRigRenderer() = default;

    // Creates the backend's GPU objects for `device` (the adapter's own handle). Returns false when
    // the host cannot support the pipeline; the caller then draws nothing this frame.
    virtual bool init(HostHandle device) = 0;

    // Uploads the skin (RGBA8, width * height * 4 bytes) to the descriptor at `slot`. The slot must
    // directly follow the scene-depth SRV: the figure samples both through one table.
    virtual bool setSkin(const uint8_t* rgba, unsigned width, unsigned height, GpuHandle slot) = 0;

    // The host's depth buffer and the descriptor slot that samples it. A null texture turns scene
    // occlusion off, and the figure is then drawn over everything.
    virtual void setSceneDepth(HostHandle depth_texture, GpuHandle slot) = 0;

    // How the host's depth buffer relates to distance (see mc/rig.hpp: reverse-Z calibration).
    virtual void setDepthConvention(const DepthConvention& convention) = 0;

    // What each hand holds and which use action it is performing, plus the atlas table the item
    // sprites are drawn from. Presentation only: the caller already resolved the hand items.
    virtual void setHeldItem(const HandItems& hands, GpuHandle atlas_table) = 0;

    // The placed blocks as one world-space mesh (mc::rig::RigMesh), drawn before the figure.
    virtual void setBlocks(const RigMesh& mesh) = 0;

    virtual void draw(const RigDraw& request) = 0;
    virtual void drawFirstPerson(const RigFirstPersonDraw& request) = 0;

    // Optional: a backend that cannot do these keeps the default no-op instead of failing the build.
    virtual void setAmbientProbe(float /*u*/, float /*v*/) {}
    virtual void setHurtTint(float /*amount*/) {}
    virtual void setGroundShadow(const RigMesh& /*mesh*/) {}
};

} // namespace mc::rig
