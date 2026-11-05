#pragma once

// Small shared value types for the D3D12 rig renderer. Deliberately free of d3d12.h so the CPU mesh
// factory and its tests build on every platform.

#include <cstdint>

namespace mc::rig::d3d12 {

using LogFn = void (*)(const char* fmt, ...);

// The shield held with the arm raised (shield_blocking.json) is stored next to the idle one under this
// bit of the item id.
inline constexpr uint16_t kBlockingKeyBit = 0x8000;

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

} // namespace mc::rig::d3d12

// Compatibility spellings for the Elden Ring adapter, its mesh factory and its tests, which still say
// erov::steve (and erov::d3d12 for the pipeline helpers). New code should use mc::rig::d3d12 directly.
namespace erov {

using mc::rig::d3d12::LogFn;
inline constexpr uint16_t kBlockingKeyBit = mc::rig::d3d12::kBlockingKeyBit;

namespace steve {
using mc::rig::d3d12::DrawRange;
using mc::rig::d3d12::FpItemCell;
} // namespace steve

namespace d3d12 = mc::rig::d3d12;

} // namespace erov
