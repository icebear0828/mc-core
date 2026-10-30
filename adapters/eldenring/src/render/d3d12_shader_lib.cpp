#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include "render/d3d12_shader_lib.hpp"

#include "render/d3d12_util.hpp"
#include "steve_shader_source.hpp" // generated at configure time from shaders/steve.hlsl

namespace erov::d3d12 {

bool Compile(const char* entry, const char* profile, ID3DBlob** out, LogFn log) {
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile(kSteveHlsl, sizeof(kSteveHlsl) - 1, "steve.hlsl", nullptr, nullptr, entry, profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &errors);
    if (FAILED(hr)) {
        if (log) log("steve: shader %s failed: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
        Rel(errors);
        return false;
    }
    Rel(errors);
    return true;
}

} // namespace erov::d3d12
