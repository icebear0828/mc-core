#pragma once

// The host camera contract every adapter needs for first person: put the view at the player's eyes, optionally
// change the vertical FOV, and put the game's own camera back afterwards. Implementations do the memory/engine
// work; the policy (when to enter, how the eye moves between stances, what exit restores) lives in
// mc::rig::FirstPersonRig.
//
// Lifecycle: save() captures the native state, writeEye()/writeFov() replace it, restore() puts it back. A rig
// calls save() at most once per session and restore() at most once, and always in the reverse order of the
// writes, so an implementation only has to remember the values captured by its last successful save().

namespace mc {

class IFirstPersonCamera {
public:
    virtual ~IFirstPersonCamera() = default;

    // Capture the native eye position and FOV. Returns false when the camera cannot be read; the rig then stays
    // inactive and writes nothing.
    virtual bool save() = 0;

    // Put the view at `eye` (world metres, Y up). Returns false when the camera is not available this frame.
    virtual bool writeEye(const float eye[3]) = 0;

    // Replace the vertical FOV, in radians. Returns false when the host cannot change it.
    virtual bool writeFov(float fov_y) = 0;

    // Put the native eye position and FOV back.
    virtual void restore() = 0;

    // Hide (or show) the native player's head so it does not fill the view.
    virtual void hideNativeHead(bool hidden) = 0;
};

} // namespace mc
