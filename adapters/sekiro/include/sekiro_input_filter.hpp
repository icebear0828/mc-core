#pragma once

// Platform independent byte-level filters for the game's DirectInput mouse data. The Windows loader hooks the
// mouse device's GetDeviceState / GetDeviceData and runs these on what the game is about to read, so the
// native character does not attack or guard while a Minecraft action is happening. Only the left and right
// buttons are cleared: camera movement, the wheel, the middle button and every keyboard key stay untouched.

#include <cstddef>
#include <cstdint>

namespace sekiro::input {

inline constexpr size_t kMouseButtonsOffset = 12; // DIMOUSESTATE: lX, lY, lZ, then rgbButtons[]
inline constexpr uint32_t kMouseButton0Offset = 12; // DIMOFS_BUTTON0 (left)
inline constexpr uint32_t kMouseButton1Offset = 13; // DIMOFS_BUTTON1 (right)

// `state` is what GetDeviceState just filled. Acts only when `size` is that of DIMOUSESTATE (16) or
// DIMOUSESTATE2 (20); anything else (a keyboard's 256 bytes, a gamepad) is left alone.
void suppressMouseButtons(void* state, size_t size);

// Buffered data: an array of DIDEVICEOBJECTDATA (dwOfs at +0, dwData at +4, `stride` bytes each).
// Button events for the two buttons get dwData = 0 (a release), so a held click never reaches the game.
void suppressBufferedMouseButtons(void* elements, size_t count, size_t stride);

} // namespace sekiro::input
