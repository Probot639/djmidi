#include "input.h"

namespace djmidi {

// VKs that need the extended-key flag set when sent as a scan code,
// otherwise Windows reports the wrong key to the foreground app.
static bool is_extended(WORD vk) {
    switch (vk) {
        case VK_RMENU:   case VK_RCONTROL:
        case VK_INSERT:  case VK_DELETE:
        case VK_HOME:    case VK_END:
        case VK_PRIOR:   case VK_NEXT:
        case VK_LEFT:    case VK_UP:     case VK_RIGHT:  case VK_DOWN:
        case VK_DIVIDE:  case VK_NUMLOCK:
            return true;
        default:
            return false;
    }
}

static void send_key(WORD vk, bool up) {
    INPUT in{};
    in.type      = INPUT_KEYBOARD;
    in.ki.wVk    = 0;   // 0 means "use the scan code in wScan"
    in.ki.wScan  = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));

    DWORD flags = KEYEVENTF_SCANCODE;
    if (is_extended(vk)) flags |= KEYEVENTF_EXTENDEDKEY;
    if (up)              flags |= KEYEVENTF_KEYUP;
    in.ki.dwFlags = flags;

    SendInput(1, &in, sizeof(in));
}

void key_down(WORD vk) { send_key(vk, false); }
void key_up(WORD vk)   { send_key(vk, true);  }
void key_tap(WORD vk)  { send_key(vk, false); send_key(vk, true); }

void mouse_move(int dx, int dy) {
    INPUT in{};
    in.type        = INPUT_MOUSE;
    in.mi.dx       = dx;
    in.mi.dy       = dy;
    in.mi.dwFlags  = MOUSEEVENTF_MOVE;   // relative
    SendInput(1, &in, sizeof(in));
}

static void mouse_button(DWORD f) {
    INPUT in{};
    in.type       = INPUT_MOUSE;
    in.mi.dwFlags = f;
    SendInput(1, &in, sizeof(in));
}

void mouse_left_down()  { mouse_button(MOUSEEVENTF_LEFTDOWN);  }
void mouse_left_up()    { mouse_button(MOUSEEVENTF_LEFTUP);    }
void mouse_right_down() { mouse_button(MOUSEEVENTF_RIGHTDOWN); }
void mouse_right_up()   { mouse_button(MOUSEEVENTF_RIGHTUP);   }

void mouse_wheel(int delta) {
    INPUT in{};
    in.type         = INPUT_MOUSE;
    in.mi.mouseData = static_cast<DWORD>(delta);
    in.mi.dwFlags   = MOUSEEVENTF_WHEEL;
    SendInput(1, &in, sizeof(in));
}

} // namespace djmidi
