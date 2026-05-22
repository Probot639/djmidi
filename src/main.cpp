// djmidi: FLX4 to Windows keyboard/mouse router.
//
// The winmm callback dispatches the mapping inline (lowest latency we can
// get without going to a kernel driver). The GUI thread owns the window,
// the jog idle-release poll, and the config-file mtime watcher.

#include "config.h"
#include "gui.h"
#include "input.h"
#include "jog.h"
#include "midi.h"
#include "state.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace djmidi {

// Shared state (definitions live here; declarations in state.h).
std::mutex                                          state_mu;
Config                                              g_cfg;
std::unordered_map<uint16_t, std::unique_ptr<Jog>>  g_jogs;
std::unordered_map<uint16_t, std::unique_ptr<Knob>> g_knobs;
std::string                                         g_config_path;

static unsigned long long cc_seen = 0;

static const Profile* active_profile_locked() {
    auto it = g_cfg.profiles.find(g_cfg.active_profile);
    return (it == g_cfg.profiles.end()) ? nullptr : &it->second;
}

static void rebuild_cc_dispatch_locked() {
    g_jogs.clear();
    g_knobs.clear();
    const Profile* p = active_profile_locked();
    if (!p) return;
    for (const auto& kv : p->ccs) {
        if (kv.second.type == ActionType::Jog) {
            auto j = std::make_unique<Jog>();
            j->configure(kv.second.jog);
            g_jogs[kv.first] = std::move(j);
        } else if (kv.second.type == ActionType::Knob) {
            auto k = std::make_unique<Knob>();
            k->configure(kv.second.knob);
            g_knobs[kv.first] = std::move(k);
        }
    }
}

bool load_and_apply(const std::string& path, std::string& error) {
    Config newcfg;
    if (!load_config(path, newcfg, error)) return false;

    std::lock_guard<std::mutex> lk(state_mu);
    g_cfg = std::move(newcfg);
    rebuild_cc_dispatch_locked();
    return true;
}

static void on_midi(const MidiEvent& ev) {
    gui_post_midi(ev);

    uint8_t type   = ev.status & 0xF0;
    uint8_t ch_1ix = (ev.status & 0x0F) + 1u;

    std::lock_guard<std::mutex> lk(state_mu);
    const Profile* p = active_profile_locked();
    if (!p) return;

    if (type == 0x90 || type == 0x80) {
        bool     down = (type == 0x90) && (ev.data2 > 0);
        uint16_t k    = bind_key(ch_1ix, ev.data1);
        auto     it   = p->notes.find(k);
        if (it == p->notes.end()) return;
        const Action& a = it->second;

        switch (a.type) {
            case ActionType::KeyTap:
                if (down) key_tap(a.key);
                return;
            case ActionType::KeyHold:
                if (down) key_down(a.key); else key_up(a.key);
                return;
            case ActionType::MouseButton:
                if (down) {
                    if      (a.mbtn == MouseBtn::Left)  mouse_left_down();
                    else if (a.mbtn == MouseBtn::Right) mouse_right_down();
                } else {
                    if      (a.mbtn == MouseBtn::Left)  mouse_left_up();
                    else if (a.mbtn == MouseBtn::Right) mouse_right_up();
                }
                return;
            default:
                return;
        }
    }

    if (type == 0xB0) {
        uint16_t k = bind_key(ch_1ix, ev.data1);
        if (auto jit = g_jogs.find(k); jit != g_jogs.end()) {
            jit->second->on_value(ev.data2);
        } else if (auto kit = g_knobs.find(k); kit != g_knobs.end()) {
            kit->second->on_value(ev.data2);
        } else {
            return;
        }
        if ((++cc_seen % 200) == 0) {
            std::printf("(cc: %llu dispatched, latest ch=%u cc=0x%02X val=%u)\n",
                        (unsigned long long)cc_seen, ch_1ix, ev.data1, ev.data2);
            std::fflush(stdout);
        }
    }
}

} // namespace djmidi

int main(int argc, char** argv) {
    using namespace djmidi;

    g_config_path = "config/default.json";
    if (argc >= 2) g_config_path = argv[1];

    std::string err;
    if (!load_and_apply(g_config_path, err)) {
        std::fprintf(stderr, "config load failed: %s\n", err.c_str());
        std::fprintf(stderr, "see config/default.json for an example.\n");
        return 3;
    }

    std::string dev_match;
    {
        std::lock_guard<std::mutex> lk(state_mu);
        dev_match = g_cfg.device_name_match;
    }

    MidiInput mi;
    int dev = mi.find_device(dev_match);
    if (dev < 0) {
        std::fprintf(stderr,
            "no MIDI device matching '%s'. plug it in and switch to MIDI mode.\n\n",
            dev_match.c_str());
        print_device_list();
        return 1;
    }

    if (!mi.open(dev, on_midi)) {
        std::fprintf(stderr, "midiInOpen failed for device %d\n", dev);
        return 2;
    }

    std::printf("djmidi v0.8 starting.\n");
    std::printf("  device : %d (matched on '%s')\n", dev, dev_match.c_str());
    std::printf("  config : %s\n", g_config_path.c_str());
    std::printf("  profile: %s\n", g_cfg.active_profile.c_str());
    std::printf("opening GUI window. close it (or ctrl+c) to quit.\n\n");

    return gui_run(GetModuleHandleA(NULL));
}
