#pragma once

// Small shared value types for the Steve D3D12 renderer. Deliberately free of d3d12.h so the CPU mesh
// factory and its tests build on every platform.

#include <cstdint>

namespace erov {

using LogFn = void (*)(const char* fmt, ...);

// The shield held with the arm raised (shield_blocking.json) is stored next to the idle one under this
// bit of the item id.
inline constexpr uint16_t kBlockingKeyBit = 0x8000;

namespace steve {

// One atlas cell of an item, in atlas pixels.
struct FpItemCell {
    uint16_t item{0};
    int x{0}, y{0}, w{0}, h{0};
};

// One draw range inside a shared vertex/index buffer.
struct DrawRange {
    unsigned index_count{0}, first_index{0};
    int base_vertex{0};
};

} // namespace steve
} // namespace erov
