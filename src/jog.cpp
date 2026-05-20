#include "jog.h"
#include "input.h"

namespace djmidi {

Jog::~Jog() {
    std::lock_guard<std::mutex> lk(mu_);
    release_held_locked();
}

void Jog::configure(const JogConfig& c) {
    std::lock_guard<std::mutex> lk(mu_);
    release_held_locked();
    cfg_           = c;
    accum_         = 0;
    last_event_ms_ = 0;
}

void Jog::release_held_locked() {
    if (held_left_)  { key_up(cfg_.key_left);  held_left_  = false; }
    if (held_right_) { key_up(cfg_.key_right); held_right_ = false; }
}

void Jog::on_value(uint8_t value) {
    std::lock_guard<std::mutex> lk(mu_);

    int delta = static_cast<int>(value) - 64;
    if (delta >= -cfg_.deadzone && delta <= cfg_.deadzone) return;

    last_event_ms_ = GetTickCount();

    switch (cfg_.mode) {
        case JogMode::None:
            return;

        case JogMode::MouseX:
            mouse_move(delta * cfg_.mouse_scale, 0);
            return;

        case JogMode::MouseY:
            mouse_move(0, delta * cfg_.mouse_scale);
            return;

        case JogMode::HeldKey:
            if (delta >= cfg_.hold_threshold) {
                if (held_left_)   { key_up(cfg_.key_left);   held_left_  = false; }
                if (!held_right_) { key_down(cfg_.key_right); held_right_ = true;  }
            } else if (delta <= -cfg_.hold_threshold) {
                if (held_right_)  { key_up(cfg_.key_right);  held_right_ = false; }
                if (!held_left_)  { key_down(cfg_.key_left); held_left_  = true;  }
            }
            return;

        case JogMode::TapPerN:
            accum_ += delta;
            while (accum_ >= cfg_.ticks_per_tap) {
                key_tap(cfg_.tap_right);
                accum_ -= cfg_.ticks_per_tap;
            }
            while (accum_ <= -cfg_.ticks_per_tap) {
                key_tap(cfg_.tap_left);
                accum_ += cfg_.ticks_per_tap;
            }
            return;
    }
}

void Jog::poll() {
    std::lock_guard<std::mutex> lk(mu_);
    if (cfg_.mode != JogMode::HeldKey)   return;
    if (!held_left_ && !held_right_)     return;

    DWORD now = GetTickCount();
    if (now - last_event_ms_ >= cfg_.hold_timeout_ms) {
        release_held_locked();
    }
}

} // namespace djmidi
