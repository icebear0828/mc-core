#pragma once

// DirectInput 8 hooks for the dinput8 proxy (milestone 1, step 4). Elden Ring imports dinput8.dll!DirectInput8Create,
// so every device the game creates passes through here:
//   IDirectInput8::CreateDevice          - remembers which device is the mouse / keyboard,
//   IDirectInputDevice8::GetDeviceState  - counts calls; while suppression is on, clears the left/right mouse buttons,
//   IDirectInputDevice8::GetDeviceData   - same for buffered mouse data.
// Counters are logged so we learn which input API the game really reads (a mouse that never reaches these hooks cannot
// be suppressed here).

#include <windows.h>

namespace erin {

using LogFn = void (*)(const char* fmt, ...);

struct Counters {
    unsigned mouse_state{0}, mouse_data{0}, keyboard_state{0}, keyboard_data{0}, other{0};
    unsigned mouse_buttons_cleared{0};
};

void SetLog(LogFn log);
// Call right after the real DirectInput8Create succeeded; `iface` is the object it returned for `riid`.
void OnDirectInputCreated(REFIID riid, void* iface);
void SetSuppressMouseButtons(bool on);
Counters TakeCounters(); // returns the counts since the last call and resets them
// True once per left-button press seen at the DirectInput layer (read before any clearing). GetAsyncKeyState cannot be
// used for this: with exclusive DirectInput the system no longer reports the mouse buttons.
bool TakeLeftClick();
// Same for the right button (read before it is cleared); used to eat food.
bool TakeRightClick();
// Wheel notches (+ = away from the user) since the last call, read from the DirectInput mouse state.
int TakeWheelNotches();
// Mouse movement (counts) since the last call, read before any suppression. Drives the inventory's virtual cursor.
void TakeMouseDelta(int& dx, int& dy);
// While on, the game's mouse reads show no movement and no wheel (the inventory is open and owns the mouse).
void SetSuppressMouseMotion(bool on);
// While on, the game's keyboard reads show nothing pressed (the inventory is open: no walking, rolling, Esc menu...).
void SetSuppressKeyboard(bool on);
// How many keyboard polls / mouse polls were blanked since the last call (diagnostics).
void TakeSuppressStats(unsigned& keys_zeroed, unsigned& motion_zeroed);

} // namespace erin
