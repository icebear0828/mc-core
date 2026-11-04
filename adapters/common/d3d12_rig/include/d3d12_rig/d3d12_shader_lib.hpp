#pragma once

// Compiles rig HLSL at runtime with D3DCompile. The caller owns the source: the adapter embeds its own
// shader text at configure time and passes it in, so this layer needs no generated header or fxc/dxc step.

#include <d3d12.h>

#include <cstddef>

#include "d3d12_rig/steve_types.hpp"

namespace mc::rig::d3d12 {

// Compiles one entry point of `hlsl` (byte length `hlsl_size`, no terminator required). `out` receives the
// bytecode on success.
[[nodiscard]] bool Compile(const char* hlsl, size_t hlsl_size, const char* entry, const char* profile, ID3DBlob** out,
                           LogFn log);

} // namespace mc::rig::d3d12
