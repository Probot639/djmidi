#include "knob.h"
#include "input.h"

namespace djmidi {

Knob::~Knob() {
    std::lock_guard<std::mutex> lk(mu_);
    release_held_locked();
}

void Knob::configure(const KnobConfig& c) {
    std::lock_guard<std::mutex> lk(mu_);
    release_held_locked();
    cfg_        = c;
    last_value_ = -1;
    accum_      = 0;
}

void Knob::release_held_locked() {
    if (held_low_)  { key_up(cfg_.key_low);  held_low_  = false; }
    if (held_high_) { key_up(cfg_.key_high); held_high_ = false; }
}

void Knob::apply_threshold_locked(int value) {
    bool want_low  = value < cfg_.low_threshold;
    bool want_high = value > cfg_.high_threshold;

    if (want_low  && !held_low_)  { key_down(cfg_.key_low);  held_low_  = true;  }
    if (!want_low &&  held_low_)  { key_up  (cfg_.key_low);  held_low_  = false; }
    if (want_high && !held_high_) { key_down(cfg_.key_high); held_high_ = true;  }
    if (!want_high&&  held_high_) { key_up  (cfg_.key_high); held_high_ = false; }
}

void Knob::on_value(uint8_t value) {
    std::lock_guard<std::mutex> lk(mu_);
    int v = static_cast<int>(value);

    switch (cfg_.mode) {
        case KnobMode::None:
            return;

        case KnobMode::Threshold:
            apply_threshold_locked(v);
            last_value_ = v;
            return;

        case KnobMode::WheelScroll: {
            if (last_value_ < 0) { last_value_ = v; return; }   // first sample sets baseline
            accum_ += (v - last_value_);
            int sign = cfg_.invert ? -1 : 1;
            while (accum_ >=  cfg_.units_per_tick) { mouse_wheel( sign * WHEEL_DELTA); accum_ -= cfg_.units_per_tick; }
            while (accum_ <= -cfg_.units_per_tick) { mouse_wheel(-sign * WHEEL_DELTA); accum_ += cfg_.units_per_tick; }
            last_value_ = v;
            return;
        }

        case KnobMode::DirectionTap: {
            if (last_value_ < 0) { last_value_ = v; return; }
            accum_ += (v - last_value_);
            while (accum_ >=  cfg_.units_per_tap) { key_tap(cfg_.key_inc); accum_ -= cfg_.units_per_tap; }
            while (accum_ <= -cfg_.units_per_tap) { key_tap(cfg_.key_dec); accum_ += cfg_.units_per_tap; }
            last_value_ = v;
            return;
        }
    }
}

} // namespace djmidi
