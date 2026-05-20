// Profile / mapping config, loaded from JSON.
//
// File layout (config/default.json):
// {
//   "device_name_match": "FLX4",
//   "active_profile":    "default",
//   "profiles": {
//     "default": {
//       "description": "...",
//       "notes": {
//         "1:0x0B": { "type": "key_tap", "key": "VK_SPACE" },
//         "1:0x36": { "type": "mouse_button", "button": "right" },
//         "8:0x00": { "type": "key_tap", "key": "1" }
//       },
//       "ccs": {
//         "1:0x21": { "type": "jog", "mode": "mouse_x", "deadzone": 1, "mouse_scale": 5 },
//         "2:0x21": { "type": "jog", "mode": "held_key", "key_left": "A", "key_right": "D",
//                     "deadzone": 1, "hold_threshold": 2, "hold_timeout_ms": 120 }
//       }
//     }
//   }
// }
//
// Binding keys are "<channel-1-indexed>:<note-or-cc-hex-or-decimal>".
// Channel is 1..16 to match how every DJ-controller doc numbers them.

#pragma once

#include "jog.h"
#include "knob.h"

#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace djmidi {

enum class ActionType {
    None,
    KeyTap,         // tap on note-on, ignore note-off
    KeyHold,        // hold key down while note is held
    MouseButton,    // hold mouse button while note is held
    Jog,            // relative CC (deltas around 64) -> Jog instance
    Knob,           // absolute CC (0..127) -> Knob instance
};

enum class MouseBtn { Left, Right, Middle };

struct Action {
    ActionType type = ActionType::None;
    WORD       key  = 0;
    MouseBtn   mbtn = MouseBtn::Left;
    JogConfig  jog;
    KnobConfig knob;
};

struct Profile {
    std::string description;
    std::unordered_map<uint16_t, Action> notes;
    std::unordered_map<uint16_t, Action> ccs;
};

struct Config {
    std::string device_name_match = "FLX4";
    std::string active_profile    = "default";
    std::unordered_map<std::string, Profile> profiles;
};

// Pack (channel, number) into a single 16-bit key for the maps.
// channel is 1-indexed (1..16).
inline uint16_t bind_key(unsigned ch_1indexed, unsigned num) {
    return static_cast<uint16_t>((ch_1indexed << 8) | (num & 0xFF));
}

// Load and parse a JSON config file. Returns true on success. On failure,
// `error_msg` contains a description and `out` is left untouched.
bool load_config(const std::string& path, Config& out, std::string& error_msg);

// Serialize Config back to JSON and write it to `path`. Used by the GUI
// when MIDI-learn commits a new binding.
bool save_config(const std::string& path, const Config& cfg, std::string& error_msg);

// Resolve a key string like "VK_SPACE", "A", "1", "F1" to a virtual-key code.
// Returns 0 if unrecognized.
WORD vk_from_string(const std::string& s);

// Reverse of vk_from_string. Returns "A".."Z", "0".."9", or "VK_xxx".
// Falls through to "0xNN" for codes we don't have a name for.
std::string vk_to_string(WORD vk);

} // namespace djmidi
