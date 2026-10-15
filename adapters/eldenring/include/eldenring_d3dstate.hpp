#pragma once

// D3D12_RESOURCE_STATES as text, for the depth probe's log (the values are the API's, so this does not need d3d12.h).

#include <cstdint>
#include <cstdio>
#include <string>

namespace eldenring::d3d {

inline std::string describeResourceState(uint32_t state) {
    if (state == 0) return "COMMON/PRESENT";
    struct Bit { uint32_t bit; const char* name; };
    static constexpr Bit kBits[] = {
        {0x1, "VERTEX_AND_CONSTANT_BUFFER"}, {0x2, "INDEX_BUFFER"},      {0x4, "RENDER_TARGET"},  {0x8, "UNORDERED_ACCESS"},
        {0x10, "DEPTH_WRITE"},               {0x20, "DEPTH_READ"},       {0x40, "NON_PIXEL_SHADER_RESOURCE"}, {0x80, "PIXEL_SHADER_RESOURCE"},
        {0x100, "STREAM_OUT"},               {0x200, "INDIRECT_ARGUMENT"}, {0x400, "COPY_DEST"},    {0x800, "COPY_SOURCE"},
        {0x1000, "RESOLVE_DEST"},            {0x2000, "RESOLVE_SOURCE"},
    };
    std::string out;
    uint32_t rest = state;
    for (const Bit& b : kBits) {
        if ((state & b.bit) == 0) continue;
        if (!out.empty()) out += '|';
        out += b.name;
        rest &= ~b.bit;
    }
    if (rest != 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%X", rest);
        if (!out.empty()) out += '|';
        out += buf;
    }
    return out;
}

} // namespace eldenring::d3d
