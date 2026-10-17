#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include "eldenring_live.hpp"
#include "eldenring_sigscan.hpp"
#include "eldenring_singletons.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace eldenring::memory {

template <typename F>
bool SehGuard(F& f) {
#ifdef _WIN32
    __try {
        f();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    try {
        f();
        return true;
    } catch (...) {
        return false;
    }
#endif
}

bool SafeCopy(uintptr_t address, void* out, size_t size);
bool SafeWrite(uintptr_t address, const void* in, size_t size);
bool SafeWrite8(uintptr_t address, uint8_t value);
bool SafeWrite32(uintptr_t address, uint32_t value);

class SelfReader final : public live::IMemoryReader {
public:
    bool read(uintptr_t address, void* out, size_t size) const override {
        return SafeCopy(address, out, size);
    }
};

struct ImageInfo {
    uintptr_t base{0};
    uint32_t image_size{0};
    uint32_t text_rva{0};
    uint32_t text_size{0};
};

bool ReadImageInfo(ImageInfo& out);

struct Bound {
    uint32_t rva{0};
    bool ok{false};
};

Bound LocateSingleton(const ImageInfo& img, const live::SingletonSig& sig);
bool LocateByPrefix(const ImageInfo& img, const char* pattern, uintptr_t& address);

} // namespace eldenring::memory
