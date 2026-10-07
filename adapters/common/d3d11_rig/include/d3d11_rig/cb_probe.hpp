#pragma once

// Read-only probe: which D3D11 constant buffers carry a given world-space position (the camera's, read from
// the host's memory)? Hooks Map/Unmap/UpdateSubresource of the immediate and deferred contexts, scans every
// constant-buffer write for the float triple and logs the buffer size, the offset and the floats around it.
// The goal is to find where the renderer's view data lives so a first-person view can be injected without
// touching the host's own camera state.

#include <d3d11.h>

namespace mc::d3d11::cbprobe {

// `set` 0 = immediate context, 1 = deferred contexts (their vtable is shared). Safe to call once per set.
bool installOn(int set, ID3D11DeviceContext* context);
void setLog(void (*log)(const char*, ...));
// Positions to look for (host world space, host axis order). Call every frame; `tag` names the target.
void setTargets(const float camera[3], const float player[3]);
void enable(bool on);

} // namespace mc::d3d11::cbprobe
