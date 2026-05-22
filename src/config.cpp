#include "config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

using nlohmann::json;

namespace djmidi {

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}

// Parse "1:0x21" or "8:0" into (channel, number). Throws on bad input.
static uint16_t parse_bind_key(const std::string& s) {
    auto colon = s.find(':');
    if (colon == std::string::npos)
        throw std::runtime_error("binding key missing ':' separator: " + s);

    int ch  = std::stoi(s.substr(0, colon), nullptr, 10);
    int num = std::stoi(s.substr(colon + 1), nullptr, 0);   // 0 -> auto base

    if (ch < 1 || ch > 16)
        throw std::runtime_error("binding channel out of range 1..16: " + s);
    if (num < 0 || num > 127)
        throw std::runtime_error("binding number out of range 0..127: " + s);

    return bind_key(static_cast<unsigned>(ch), static_cast<unsigned>(num));
}

// VK lookup. Not exhaustive, extend as needed. Match is upper-cased.
WORD vk_from_string(const std::string& s_in) {
    std::string s = to_upper(s_in);
    if (s.empty()) return 0;

    // single character A..Z, 0..9
    if (s.size() == 1) {
        char c = s[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<WORD>(c);
        }
    }

    // VK_xxx aliases
    static const std::unordered_map<std::string, WORD> table = {
        {"VK_SPACE",   VK_SPACE},   {"SPACE",  VK_SPACE},
        {"VK_RETURN",  VK_RETURN},  {"RETURN", VK_RETURN}, {"ENTER", VK_RETURN},
        {"VK_BACK",    VK_BACK},    {"BACK",   VK_BACK},   {"BACKSPACE", VK_BACK},
        {"VK_TAB",     VK_TAB},     {"TAB",    VK_TAB},
        {"VK_ESCAPE",  VK_ESCAPE},  {"ESC",    VK_ESCAPE}, {"ESCAPE",    VK_ESCAPE},
        {"VK_DELETE",  VK_DELETE},  {"DEL",    VK_DELETE}, {"DELETE",    VK_DELETE},
        {"VK_INSERT",  VK_INSERT},  {"INSERT", VK_INSERT},
        {"VK_HOME",    VK_HOME},    {"HOME",   VK_HOME},
        {"VK_END",     VK_END},     {"END",    VK_END},
        {"VK_PRIOR",   VK_PRIOR},   {"PAGEUP", VK_PRIOR},
        {"VK_NEXT",    VK_NEXT},    {"PAGEDOWN", VK_NEXT},
        {"VK_LEFT",    VK_LEFT},    {"LEFT",   VK_LEFT},
        {"VK_RIGHT",   VK_RIGHT},   {"RIGHT",  VK_RIGHT},
        {"VK_UP",      VK_UP},      {"UP",     VK_UP},
        {"VK_DOWN",    VK_DOWN},    {"DOWN",   VK_DOWN},
        {"VK_LSHIFT",  VK_LSHIFT},  {"LSHIFT", VK_LSHIFT}, {"SHIFT", VK_LSHIFT},
        {"VK_RSHIFT",  VK_RSHIFT},  {"RSHIFT", VK_RSHIFT},
        {"VK_LCONTROL",VK_LCONTROL},{"LCONTROL", VK_LCONTROL}, {"LCTRL", VK_LCONTROL}, {"CTRL", VK_LCONTROL},
        {"VK_RCONTROL",VK_RCONTROL},{"RCONTROL", VK_RCONTROL}, {"RCTRL", VK_RCONTROL},
        {"VK_LMENU",   VK_LMENU},   {"LALT",   VK_LMENU}, {"ALT", VK_LMENU},
        {"VK_RMENU",   VK_RMENU},   {"RALT",   VK_RMENU},
        {"VK_F1", VK_F1},  {"F1", VK_F1},
        {"VK_F2", VK_F2},  {"F2", VK_F2},
        {"VK_F3", VK_F3},  {"F3", VK_F3},
        {"VK_F4", VK_F4},  {"F4", VK_F4},
        {"VK_F5", VK_F5},  {"F5", VK_F5},
        {"VK_F6", VK_F6},  {"F6", VK_F6},
        {"VK_F7", VK_F7},  {"F7", VK_F7},
        {"VK_F8", VK_F8},  {"F8", VK_F8},
        {"VK_F9", VK_F9},  {"F9", VK_F9},
        {"VK_F10",VK_F10}, {"F10",VK_F10},
        {"VK_F11",VK_F11}, {"F11",VK_F11},
        {"VK_F12",VK_F12}, {"F12",VK_F12},
    };
    auto it = table.find(s);
    return it == table.end() ? WORD{0} : it->second;
}

static MouseBtn parse_mouse_btn(const std::string& s) {
    std::string u = to_upper(s);
    if (u == "LEFT")   return MouseBtn::Left;
    if (u == "RIGHT")  return MouseBtn::Right;
    if (u == "MIDDLE") return MouseBtn::Middle;
    throw std::runtime_error("unknown mouse button: " + s);
}

static JogMode parse_jog_mode(const std::string& s) {
    std::string u = to_upper(s);
    if (u == "MOUSE_X")     return JogMode::MouseX;
    if (u == "MOUSE_Y")     return JogMode::MouseY;
    if (u == "HELD_KEY")    return JogMode::HeldKey;
    if (u == "TAP_PER_N")   return JogMode::TapPerN;
    if (u == "NONE")        return JogMode::None;
    throw std::runtime_error("unknown jog mode: " + s);
}

static KnobMode parse_knob_mode(const std::string& s) {
    std::string u = to_upper(s);
    if (u == "WHEEL_SCROLL")  return KnobMode::WheelScroll;
    if (u == "THRESHOLD")     return KnobMode::Threshold;
    if (u == "DIRECTION_TAP") return KnobMode::DirectionTap;
    if (u == "NONE")          return KnobMode::None;
    throw std::runtime_error("unknown knob mode: " + s);
}

static Action parse_action(const json& j) {
    Action a;
    std::string t = j.at("type").get<std::string>();

    if (t == "key_tap") {
        a.type = ActionType::KeyTap;
        a.key  = vk_from_string(j.at("key").get<std::string>());
        if (!a.key) throw std::runtime_error("key_tap: unknown key '" + j.at("key").get<std::string>() + "'");
    } else if (t == "key_hold") {
        a.type = ActionType::KeyHold;
        a.key  = vk_from_string(j.at("key").get<std::string>());
        if (!a.key) throw std::runtime_error("key_hold: unknown key '" + j.at("key").get<std::string>() + "'");
    } else if (t == "mouse_button") {
        a.type = ActionType::MouseButton;
        a.mbtn = parse_mouse_btn(j.at("button").get<std::string>());
    } else if (t == "jog") {
        a.type = ActionType::Jog;
        a.jog.mode = parse_jog_mode(j.at("mode").get<std::string>());
        if (j.contains("deadzone"))        a.jog.deadzone        = j.at("deadzone").get<int>();
        if (j.contains("mouse_scale"))     a.jog.mouse_scale     = j.at("mouse_scale").get<int>();
        if (j.contains("key_left"))        a.jog.key_left        = vk_from_string(j.at("key_left").get<std::string>());
        if (j.contains("key_right"))       a.jog.key_right       = vk_from_string(j.at("key_right").get<std::string>());
        if (j.contains("hold_threshold"))  a.jog.hold_threshold  = j.at("hold_threshold").get<int>();
        if (j.contains("hold_timeout_ms")) a.jog.hold_timeout_ms = j.at("hold_timeout_ms").get<DWORD>();
        if (j.contains("tap_left"))        a.jog.tap_left        = vk_from_string(j.at("tap_left").get<std::string>());
        if (j.contains("tap_right"))       a.jog.tap_right       = vk_from_string(j.at("tap_right").get<std::string>());
        if (j.contains("ticks_per_tap"))   a.jog.ticks_per_tap   = j.at("ticks_per_tap").get<int>();
    } else if (t == "knob") {
        a.type = ActionType::Knob;
        a.knob.mode = parse_knob_mode(j.at("mode").get<std::string>());
        if (j.contains("units_per_tick")) a.knob.units_per_tick = j.at("units_per_tick").get<int>();
        if (j.contains("invert"))         a.knob.invert         = j.at("invert").get<bool>();
        if (j.contains("low_threshold"))  a.knob.low_threshold  = j.at("low_threshold").get<int>();
        if (j.contains("high_threshold")) a.knob.high_threshold = j.at("high_threshold").get<int>();
        if (j.contains("key_low"))        a.knob.key_low        = vk_from_string(j.at("key_low").get<std::string>());
        if (j.contains("key_high"))       a.knob.key_high       = vk_from_string(j.at("key_high").get<std::string>());
        if (j.contains("key_inc"))        a.knob.key_inc        = vk_from_string(j.at("key_inc").get<std::string>());
        if (j.contains("key_dec"))        a.knob.key_dec        = vk_from_string(j.at("key_dec").get<std::string>());
        if (j.contains("units_per_tap"))  a.knob.units_per_tap  = j.at("units_per_tap").get<int>();
    } else {
        throw std::runtime_error("unknown action type: " + t);
    }
    return a;
}

static Profile parse_profile(const json& j) {
    Profile p;
    if (j.contains("description")) p.description = j.at("description").get<std::string>();

    if (j.contains("notes") && j.at("notes").is_object()) {
        for (auto it = j.at("notes").begin(); it != j.at("notes").end(); ++it) {
            uint16_t k = parse_bind_key(it.key());
            p.notes[k] = parse_action(it.value());
        }
    }
    if (j.contains("ccs") && j.at("ccs").is_object()) {
        for (auto it = j.at("ccs").begin(); it != j.at("ccs").end(); ++it) {
            uint16_t k = parse_bind_key(it.key());
            p.ccs[k] = parse_action(it.value());
        }
    }
    return p;
}

std::string vk_to_string(WORD vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z'))
        return std::string(1, static_cast<char>(vk));

    switch (vk) {
        case VK_SPACE:    return "VK_SPACE";
        case VK_RETURN:   return "VK_RETURN";
        case VK_BACK:     return "VK_BACK";
        case VK_TAB:      return "VK_TAB";
        case VK_ESCAPE:   return "VK_ESCAPE";
        case VK_DELETE:   return "VK_DELETE";
        case VK_INSERT:   return "VK_INSERT";
        case VK_HOME:     return "VK_HOME";
        case VK_END:      return "VK_END";
        case VK_PRIOR:    return "VK_PRIOR";
        case VK_NEXT:     return "VK_NEXT";
        case VK_LEFT:     return "VK_LEFT";
        case VK_RIGHT:    return "VK_RIGHT";
        case VK_UP:       return "VK_UP";
        case VK_DOWN:     return "VK_DOWN";
        case VK_LSHIFT:   return "VK_LSHIFT";
        case VK_RSHIFT:   return "VK_RSHIFT";
        case VK_LCONTROL: return "VK_LCONTROL";
        case VK_RCONTROL: return "VK_RCONTROL";
        case VK_LMENU:    return "VK_LMENU";
        case VK_RMENU:    return "VK_RMENU";
        case VK_F1:  return "VK_F1";   case VK_F2:  return "VK_F2";
        case VK_F3:  return "VK_F3";   case VK_F4:  return "VK_F4";
        case VK_F5:  return "VK_F5";   case VK_F6:  return "VK_F6";
        case VK_F7:  return "VK_F7";   case VK_F8:  return "VK_F8";
        case VK_F9:  return "VK_F9";   case VK_F10: return "VK_F10";
        case VK_F11: return "VK_F11";  case VK_F12: return "VK_F12";
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%02X", static_cast<unsigned>(vk));
    return buf;
}

static std::string format_bind_key(uint16_t k) {
    unsigned ch  = (k >> 8) & 0xFF;
    unsigned num = k & 0xFF;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%u:0x%02X", ch, num);
    return buf;
}

static const char* jog_mode_name(JogMode m) {
    switch (m) {
        case JogMode::MouseX:  return "mouse_x";
        case JogMode::MouseY:  return "mouse_y";
        case JogMode::HeldKey: return "held_key";
        case JogMode::TapPerN: return "tap_per_n";
        default:               return "none";
    }
}

static const char* knob_mode_name(KnobMode m) {
    switch (m) {
        case KnobMode::WheelScroll:  return "wheel_scroll";
        case KnobMode::Threshold:    return "threshold";
        case KnobMode::DirectionTap: return "direction_tap";
        default:                     return "none";
    }
}

static const char* mouse_btn_name(MouseBtn b) {
    switch (b) {
        case MouseBtn::Right:  return "right";
        case MouseBtn::Middle: return "middle";
        case MouseBtn::Left:
        default:               return "left";
    }
}

static json action_to_json(const Action& a) {
    json j;
    switch (a.type) {
        case ActionType::KeyTap:
            j["type"] = "key_tap";
            j["key"]  = vk_to_string(a.key);
            break;
        case ActionType::KeyHold:
            j["type"] = "key_hold";
            j["key"]  = vk_to_string(a.key);
            break;
        case ActionType::MouseButton:
            j["type"]   = "mouse_button";
            j["button"] = mouse_btn_name(a.mbtn);
            break;
        case ActionType::Jog:
            j["type"]            = "jog";
            j["mode"]            = jog_mode_name(a.jog.mode);
            j["deadzone"]        = a.jog.deadzone;
            j["mouse_scale"]     = a.jog.mouse_scale;
            j["hold_threshold"]  = a.jog.hold_threshold;
            j["hold_timeout_ms"] = a.jog.hold_timeout_ms;
            j["ticks_per_tap"]   = a.jog.ticks_per_tap;
            if (a.jog.key_left)  j["key_left"]  = vk_to_string(a.jog.key_left);
            if (a.jog.key_right) j["key_right"] = vk_to_string(a.jog.key_right);
            if (a.jog.tap_left)  j["tap_left"]  = vk_to_string(a.jog.tap_left);
            if (a.jog.tap_right) j["tap_right"] = vk_to_string(a.jog.tap_right);
            break;
        case ActionType::Knob:
            j["type"]            = "knob";
            j["mode"]            = knob_mode_name(a.knob.mode);
            j["units_per_tick"]  = a.knob.units_per_tick;
            j["invert"]          = a.knob.invert;
            j["low_threshold"]   = a.knob.low_threshold;
            j["high_threshold"]  = a.knob.high_threshold;
            j["units_per_tap"]   = a.knob.units_per_tap;
            if (a.knob.key_low)  j["key_low"]  = vk_to_string(a.knob.key_low);
            if (a.knob.key_high) j["key_high"] = vk_to_string(a.knob.key_high);
            if (a.knob.key_inc)  j["key_inc"]  = vk_to_string(a.knob.key_inc);
            if (a.knob.key_dec)  j["key_dec"]  = vk_to_string(a.knob.key_dec);
            break;
        default:
            j["type"] = "none";
            break;
    }
    return j;
}

bool save_config(const std::string& path, const Config& cfg, std::string& error_msg) {
    try {
        json j;
        j["device_name_match"] = cfg.device_name_match;
        j["active_profile"]    = cfg.active_profile;

        json profiles = json::object();
        for (const auto& pkv : cfg.profiles) {
            json jp;
            if (!pkv.second.description.empty())
                jp["description"] = pkv.second.description;

            json notes = json::object();
            for (const auto& nkv : pkv.second.notes)
                notes[format_bind_key(nkv.first)] = action_to_json(nkv.second);
            jp["notes"] = notes;

            json ccs = json::object();
            for (const auto& ckv : pkv.second.ccs)
                ccs[format_bind_key(ckv.first)] = action_to_json(ckv.second);
            jp["ccs"] = ccs;

            profiles[pkv.first] = jp;
        }
        j["profiles"] = profiles;

        std::ofstream f(path);
        if (!f.is_open()) {
            error_msg = "could not open for writing: " + path;
            return false;
        }
        f << j.dump(2) << "\n";
        return true;
    } catch (const std::exception& e) {
        error_msg = std::string("save error: ") + e.what();
        return false;
    }
}

bool load_config(const std::string& path, Config& out, std::string& error_msg) {
    std::ifstream f(path);
    if (!f.is_open()) {
        error_msg = "could not open " + path;
        return false;
    }

    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        error_msg = std::string("JSON parse error: ") + e.what();
        return false;
    }

    Config cfg;
    try {
        if (j.contains("device_name_match")) cfg.device_name_match = j.at("device_name_match").get<std::string>();
        if (j.contains("active_profile"))    cfg.active_profile    = j.at("active_profile").get<std::string>();

        if (!j.contains("profiles") || !j.at("profiles").is_object()) {
            error_msg = "no 'profiles' object at top level";
            return false;
        }
        for (auto it = j.at("profiles").begin(); it != j.at("profiles").end(); ++it) {
            cfg.profiles[it.key()] = parse_profile(it.value());
        }
    } catch (const std::exception& e) {
        error_msg = e.what();
        return false;
    }

    if (cfg.profiles.find(cfg.active_profile) == cfg.profiles.end()) {
        error_msg = "active_profile '" + cfg.active_profile + "' not found in profiles";
        return false;
    }

    out = std::move(cfg);
    return true;
}

} // namespace djmidi
