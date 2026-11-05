#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include "d3d12_rig/d3d12_shader_lib.hpp"

#include "d3d12_rig/d3d12_util.hpp"

namespace mc::rig::d3d12 {

bool Compile(const char* hlsl, size_t hlsl_size, const char* entry, const char* profile, ID3DBlob** out, LogFn log) {
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile(hlsl, hlsl_size, "steve.hlsl", nullptr, nullptr, entry, profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &errors);
    if (FAILED(hr)) {
        if (log) log("steve: shader %s failed: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
        Rel(errors);
        return false;
    }
    Rel(errors);
    return true;
}

} // namespace mc::rig::d3d12
