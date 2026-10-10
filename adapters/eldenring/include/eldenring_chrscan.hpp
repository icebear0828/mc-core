#pragma once

// Read-only structure scan of a character object (summon work, feat/summon): which of its pointers lead to objects with RTTI, and where inside
// the model-like ones a display-flags word sits. The player's own model is found through PlayerIns+0x648 (CSChrAsmModelIns, parts at +0x28,
// part+0x18 -> CSModelDispEntity, disp_flags1 at +0x20 = 0x000100A1 while drawn, eldenring_model.hpp). A summoned wolf is an ordinary monster
// model; nobody knows yet where its display flags are, so this probe lists the candidates for the log. Nothing here writes.

#include "eldenring_rtti.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace eldenring::live {

struct ScanHit {
    uintptr_t offset{0}; // from the scanned object
    uintptr_t object{0}; // what the pointer points to
    std::string cls;     // RTTI name of that object, e.g. ".?AVCSChrAsmModelIns@CS@@"
};

// Every aligned qword in [from, to) of `object` that points to an object whose vtable has x64 RTTI.
inline std::vector<ScanHit> scanForClasses(const IMemoryReader& reader, uintptr_t image_base, uintptr_t object, uintptr_t from, uintptr_t to, size_t max_hits = 96) {
    std::vector<ScanHit> out;
    if (object == 0) return out;
    for (uintptr_t off = from; off + sizeof(uint64_t) <= to && out.size() < max_hits; off += sizeof(uint64_t)) {
        uint64_t p = 0, vtable = 0;
        if (!reader.read(object + off, &p, sizeof(p)) || p < 0x10000) continue;
        if (!reader.read(static_cast<uintptr_t>(p), &vtable, sizeof(vtable)) || vtable < 0x10000) continue;
        if (const auto name = readTypeNameOfVtable(reader, image_base, vtable)) out.push_back({off, static_cast<uintptr_t>(p), *name});
    }
    return out;
}

// Offsets (from `object`) of the dwords in [from, to) that equal `value`: where a flags word with a known live value sits.
inline std::vector<uintptr_t> findDwordOffsets(const IMemoryReader& reader, uintptr_t object, uintptr_t from, uintptr_t to, uint32_t value, size_t max_hits = 16) {
    std::vector<uintptr_t> out;
    if (object == 0) return out;
    for (uintptr_t off = from; off + sizeof(uint32_t) <= to && out.size() < max_hits; off += sizeof(uint32_t)) {
        uint32_t v = 0;
        if (reader.read(object + off, &v, sizeof(v)) && v == value) out.push_back(off);
    }
    return out;
}

// The dwords of [from, to) as one line ("+0x20: 000100A1 00000001"); unreadable ones show as ???. For comparing two objects of the same class.
inline std::string dumpDwords(const IMemoryReader& reader, uintptr_t object, uintptr_t from, uintptr_t to) {
    char head[32];
    std::snprintf(head, sizeof(head), "+0x%llX:", static_cast<unsigned long long>(from));
    std::string out = head;
    for (uintptr_t off = from; off + sizeof(uint32_t) <= to; off += sizeof(uint32_t)) {
        uint32_t v = 0;
        char word[16];
        if (object != 0 && reader.read(object + off, &v, sizeof(v))) std::snprintf(word, sizeof(word), " %08X", v);
        else std::snprintf(word, sizeof(word), " ???");
        out += word;
    }
    return out;
}

// Classes worth looking inside: the model and display objects.
inline bool looksLikeModelClass(const std::string& cls) {
    return cls.find("Model") != std::string::npos || cls.find("Disp") != std::string::npos || cls.find("Mesh") != std::string::npos ||
           cls.find("Render") != std::string::npos;
}

inline std::string formatScanHit(const ScanHit& h, int depth) {
    char buf[260];
    std::snprintf(buf, sizeof(buf), "chrscan: %*s+0x%llX -> %p %s", depth * 2, "", static_cast<unsigned long long>(h.offset), reinterpret_cast<void*>(h.object), h.cls.c_str());
    return buf;
}

} // namespace eldenring::live
