#include "sekiro_input_filter.hpp"

#include <cstring>

namespace sekiro::input {

void suppressMouseButtons(void* state, size_t size) {
    if (!state || (size != 16 && size != 20)) return;
    auto* bytes = static_cast<uint8_t*>(state);
    bytes[kMouseButton0Offset] = 0;
    bytes[kMouseButton1Offset] = 0;
}

void suppressBufferedMouseButtons(void* elements, size_t count, size_t stride) {
    if (!elements || count == 0 || stride < 8) return;
    auto* bytes = static_cast<uint8_t*>(elements);
    for (size_t i = 0; i < count; ++i) {
        uint8_t* e = bytes + i * stride;
        uint32_t ofs = 0;
        std::memcpy(&ofs, e, sizeof(ofs));
        if (ofs == kMouseButton0Offset || ofs == kMouseButton1Offset) {
            const uint32_t released = 0;
            std::memcpy(e + 4, &released, sizeof(released));
        }
    }
}

} // namespace sekiro::input
