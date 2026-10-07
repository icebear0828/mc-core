#include "sekiro_damage_probe.hpp"

#include <cstdio>
#include <cstring>

namespace sekiro::live {

std::string describeDamageData(const uint8_t* data, size_t size, const IMemoryReader& reader, uintptr_t image_base, size_t image_size) {
    std::string out;
    char line[200];
    auto known = [&](size_t off, const char* name) {
        if (off + 4 > size) return;
        uint32_t u = 0;
        float f = 0;
        std::memcpy(&u, data + off, 4);
        std::memcpy(&f, data + off, 4);
        std::snprintf(line, sizeof(line), "  +0x%03zX %-15s u32=%u (%#x) f32=%g\n", off, name, u, u, static_cast<double>(f));
        out += line;
    };
    known(0x24, "hp_damage");
    known(0x28, "posture_damage");
    known(0x54, "stagger_level");
    known(0x1E0, "posture_damage_2");

    for (size_t off = 0; off + 8 <= size; off += 8) {
        uint64_t q = 0;
        std::memcpy(&q, data + off, 8);
        if (q == 0) continue;
        const char* kind = "";
        if (q >= image_base && q < image_base + image_size) {
            kind = " <- image pointer";
        } else if (q >= 0x10000 && q < 0x00007FFFFFFFFFFFull) {
            uint8_t probe[8];
            kind = reader.read(static_cast<uintptr_t>(q), probe, sizeof(probe)) ? " <- heap pointer (readable)" : " <- heap pointer (NOT readable)";
        }
        std::snprintf(line, sizeof(line), "  +0x%03zX word %016llx%s\n", off, static_cast<unsigned long long>(q), kind);
        out += line;
    }
    return out;
}

} // namespace sekiro::live
