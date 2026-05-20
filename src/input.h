// Thin SendInput wrapper. Uses scan codes (KEYEVENTF_SCANCODE) rather
// than virtual-key codes, since DirectInput and most game engines silently
// drop synthetic VK events but accept scan-code ones as real keystrokes.

#pragma once

#include <windows.h>
#include <cstdint>

namespace djmidi {

void key_down(WORD vk);
void key_up(WORD vk);
void key_tap(WORD vk);   // down then up, back-to-back

// Relative mouse move. Absolute mode is a different code path and we
// don't need it yet.
void mouse_move(int dx, int dy);

void mouse_left_down();
void mouse_left_up();
void mouse_right_down();
void mouse_right_up();

// Vertical wheel scroll. Positive = forward (away from user, "up");
// negative = backward. WHEEL_DELTA (120) is one notch.
void mouse_wheel(int delta);

} // namespace djmidi
