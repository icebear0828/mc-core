#pragma once

// Compiles the embedded Steve HLSL (generated from adapters/eldenring/shaders/steve.hlsl) at runtime with
// D3DCompile, so the adapter keeps working without external shader files or an fxc/dxc build step.

#include <d3d12.h>

#include "render/steve_types.hpp"

namespace erov::d3d12 {

// Compiles one entry point of the Steve shader. `out` receives the bytecode on success.
[[nodiscard]] bool Compile(const char* entry, const char* profile, ID3DBlob** out, LogFn log);

} // namespace erov::d3d12
