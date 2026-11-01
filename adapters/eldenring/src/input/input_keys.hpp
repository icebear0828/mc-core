#pragma once

// Shared key polling for the input handlers. On Windows the virtual-key
// constants come from <windows.h>; the other platforms get the same values so
// the handler sources stay portable.

#include "eldenring_hotkey_dispatcher.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#ifndef VK_SPACE
#define VK_SPACE 0x20
#endif
#ifndef VK_CONTROL
#define VK_CONTROL 0x11
#endif
#ifndef VK_LBUTTON
#define VK_LBUTTON 0x01
#endif
#ifndef VK_RBUTTON
#define VK_RBUTTON 0x02
#endif
#ifndef VK_SHIFT
#define VK_SHIFT 0x10
#endif
#ifndef VK_LSHIFT
#define VK_LSHIFT 0xA0
#endif
#ifndef VK_RSHIFT
#define VK_RSHIFT 0xA1
#endif
#ifndef VK_F1
#define VK_F1 0x70
#endif
#ifndef VK_F2
#define VK_F2 0x71
#endif
#ifndef VK_F3
#define VK_F3 0x72
#endif
#ifndef VK_F4
#define VK_F4 0x73
#endif
#ifndef VK_F5
#define VK_F5 0x74
#endif
#ifndef VK_F6
#define VK_F6 0x75
#endif
#ifndef VK_F7
#define VK_F7 0x76
#endif
#ifndef VK_F8
#define VK_F8 0x77
#endif
#ifndef VK_F9
#define VK_F9 0x78
#endif
#ifndef VK_F10
#define VK_F10 0x79
#endif
#ifndef VK_F11
#define VK_F11 0x7A
#endif
#ifndef VK_F12
#define VK_F12 0x7B
#endif
#ifndef VK_PRIOR
#define VK_PRIOR 0x21
#endif
#ifndef VK_NEXT
#define VK_NEXT 0x22
#endif
#ifndef VK_OEM_1
#define VK_OEM_1 0xBA
#endif
#ifndef VK_OEM_2
#define VK_OEM_2 0xBF
#endif
#ifndef VK_OEM_4
#define VK_OEM_4 0xDB
#endif
#ifndef VK_OEM_5
#define VK_OEM_5 0xDC
#endif
#ifndef VK_OEM_6
#define VK_OEM_6 0xDD
#endif
#ifndef VK_OEM_7
#define VK_OEM_7 0xDE
#endif
#ifndef VK_OEM_COMMA
#define VK_OEM_COMMA 0xBC
#endif
#ifndef VK_OEM_PERIOD
#define VK_OEM_PERIOD 0xBE
#endif
#ifndef VK_OEM_MINUS
#define VK_OEM_MINUS 0xBD
#endif
#ifndef VK_OEM_PLUS
#define VK_OEM_PLUS 0xBB
#endif
#endif

namespace eldenring::input {

inline bool queryKeyDown(const HotkeyCallbacks& cb, int vk) {
    if (cb.is_key_down) {
        return cb.is_key_down(vk);
    }
#ifdef _WIN32
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    (void)vk;
    return false;
#endif
}

} // namespace eldenring::input
