// Knob / fader state machine. Unlike jogs, knobs send *absolute* CC
// values 0..127 (not deltas around 64). We track the previous value
// internally and derive change-driven behavior from that.
//
// Three modes:
//   WheelScroll  - each `units_per_tick` units of change scrolls the
//                  mouse wheel one notch. Good for rotary knobs.
//   Threshold    - 3-zone for faders/sliders. value < low_threshold
//                  holds key_low; value > high_threshold holds
//                  key_high; in between, nothing held.
//   DirectionTap - changing by `units_per_tap` units fires one tap of
//                  key_inc (going up) or key_dec (going down).

#pragma once

#include <windows.h>
#include <cstdint>
#include <mutex>

namespace djmidi {

enum class KnobMode {
    None,
    WheelScroll,
    Threshold,
    DirectionTap,
};

struct KnobConfig {
    KnobMode mode = KnobMode::None;

    // WheelScroll
    int  units_per_tick = 4;
    bool invert         = false;     // flip "up means up" if your knob feels reversed

    // Threshold
    int  low_threshold  = 40;
    int  high_threshold = 88;
    WORD key_low  = 0;
    WORD key_high = 0;

    // DirectionTap
    WORD key_inc = 0;
    WORD key_dec = 0;
    int  units_per_tap = 8;
};

class Knob {
public:
    ~Knob();

    void configure(const KnobConfig& c);
    void on_value(uint8_t value);

private:
    void release_held_locked();
    void apply_threshold_locked(int value);

    mutable std::mutex mu_;
    KnobConfig cfg_;
    int  last_value_ = -1;    // -1 = no value seen yet
    int  accum_      = 0;
    bool held_low_   = false;
    bool held_high_  = false;
};

} // namespace djmidi
