// Jog wheel state machine. Pioneer's wheels emit relative CC deltas
// around 64 (64 = idle, 65 = +1 tick, 63 = -1 tick). This class takes
// the raw CC value and dispatches to one of three output modes.
//
// For HeldKey mode we need an idle timeout, since the wheel doesn't
// send a "stopped" event — it just stops sending. poll() handles that
// and must be called regularly from the main thread.

#pragma once

#include <windows.h>
#include <cstdint>
#include <mutex>

namespace djmidi {

enum class JogMode {
    None,
    MouseX,    // (value - 64) * scale -> mouse dx
    MouseY,    // (value - 64) * scale -> mouse dy
    HeldKey,   // CCW past threshold -> left key held; CW -> right held; released on idle
    TapPerN,   // accumulator; every N ticks fires one tap of left or right key
};

struct JogConfig {
    JogMode mode = JogMode::None;

    int  deadzone    = 1;     // |value-64| <= this -> ignored
    int  mouse_scale = 5;     // MouseX/MouseY: pixels per (value-64)

    // HeldKey:
    WORD  key_left        = 0;
    WORD  key_right       = 0;
    int   hold_threshold  = 2;     // |delta| >= this triggers the held key
    DWORD hold_timeout_ms = 120;   // no events for this long -> release

    // TapPerN:
    WORD  tap_left      = 0;
    WORD  tap_right     = 0;
    int   ticks_per_tap = 4;
};

class Jog {
public:
    ~Jog();   // releases any HeldKey keys still pressed (matters on config reload)

    void configure(const JogConfig& c);
    void on_value(uint8_t value);   // raw CC data byte (0..127, idle = 64)
    void poll();                     // call from main thread regularly

private:
    void release_held_locked();

    mutable std::mutex mu_;
    JogConfig cfg_;
    DWORD     last_event_ms_ = 0;
    int       accum_         = 0;
    bool      held_left_     = false;
    bool      held_right_    = false;
};

} // namespace djmidi
