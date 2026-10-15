#include "eldenring_memory_scanner.hpp"

namespace eldenring::memory {

bool SafeCopy(uintptr_t address, void* out, size_t size) {
    if (size == 0) return true;
    if (address < 0x10000) return false;
#ifdef _WIN32
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
#endif
}

bool SafeWrite8(uintptr_t address, uint8_t value) {
    if (address < 0x10000) return false;
#ifdef _WIN32
    __try {
        *reinterpret_cast<volatile uint8_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    *reinterpret_cast<volatile uint8_t*>(address) = value;
    return true;
#endif
}

bool SafeWrite32(uintptr_t address, uint32_t value) {
    if (address < 0x10000) return false;
#ifdef _WIN32
    __try {
        *reinterpret_cast<volatile uint32_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    *reinterpret_cast<volatile uint32_t*>(address) = value;
    return true;
#endif
}

bool SafeWriteBytes(uintptr_t address, const void* src, size_t size) {
    if (size == 0) return true;
    if (address < 0x10000) return false;
#ifdef _WIN32
    __try {
        std::memcpy(reinterpret_cast<void*>(address), src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    std::memcpy(reinterpret_cast<void*>(address), src, size);
    return true;
#endif
}


bool ReadImageInfo(ImageInfo& out) {
#ifdef _WIN32
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base == 0) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<uintptr_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    out.base = base;
    out.image_size = nt->OptionalHeader.SizeOfImage;
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(sec[i].Name, ".text", 5) == 0) {
            out.text_rva = sec[i].VirtualAddress;
            out.text_size = sec[i].Misc.VirtualSize;
            return out.text_size > 0;
        }
    }
    return false;
#else
    (void)out;
    return false;
#endif
}

Bound LocateSingleton(const ImageInfo& img, const live::SingletonSig& sig) {
    Bound b;
    std::optional<uint32_t> result;
    auto work = [&] {
        result = live::locateGlobalRva(reinterpret_cast<const uint8_t*>(img.base + img.text_rva), img.text_size, img.text_rva,
                                       img.image_size, sig);
    };
    if (SehGuard(work) && result) {
        b.rva = *result;
        b.ok = true;
    }
    return b;
}

bool LocateByPrefix(const ImageInfo& img, const char* pattern, uintptr_t& address) {
    const auto parsed = live::Signature::parse(pattern);
    if (!parsed) return false;
    live::ScanResult r;
    auto work = [&] { r = live::scanUnique(reinterpret_cast<const uint8_t*>(img.base + img.text_rva), img.text_size, *parsed); };
    if (!SehGuard(work) || r.status != live::ScanStatus::Unique) return false;
    address = img.base + img.text_rva + r.offset;
    return true;
}

} // namespace eldenring::memory
