#pragma once

#include <d3d11.h>

#include <cstdint>

namespace sekiro::render {

// Watches which depth-stencil views the game binds so the Steve rig can be depth-tested against the
// real scene. Read-only: it only records, it never changes what the game binds.
class DepthCapture {
public:
    // Hooks ID3D11DeviceContext::OMSetRenderTargets on the immediate context. False if MinHook fails.
    bool install(ID3D11DeviceContext* immediate);
    void remove();

    // Writes every candidate (size, format, bind count) and the depth range of its current contents
    // to `log`. Debug aid, called on demand: it stalls the GPU.
    void dump(ID3D11Device* device, ID3D11DeviceContext* context, void (*log)(const char*, ...));

    // Logs the depth contents just before each of the next 12 clears of a candidate, to find the pass that
    // holds the full scene.
    void armTrace(ID3D11Device* device, void (*log)(const char*, ...));

    // Forget everything (swap chain resized).
    void reset();
};

} // namespace sekiro::render
