// MIDI input via winmm. Opens a device matching a name substring,
// dispatches messages to a user-supplied callback.

#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <functional>
#include <string>

namespace djmidi {

struct MidiEvent {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
    DWORD   ts_ms;   // ms since midiInOpen
};

using MidiCallback = std::function<void(const MidiEvent&)>;

class MidiInput {
public:
    MidiInput() = default;
    ~MidiInput();

    MidiInput(const MidiInput&)            = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    // Returns the device id (>=0) or -1 if none matched.
    // Note: winmm truncates device names to 32 chars (MAXPNAMELEN),
    // which is fine for "DDJ-FLX4" but means longer names like
    // "Pioneer DDJ-FLX4 MIDI 1" can show up clipped.
    int find_device(const std::string& name_substr);

    bool open(int dev_id, MidiCallback cb);
    void close();

    bool is_open() const { return handle_ != nullptr; }

private:
    static void CALLBACK midi_proc(HMIDIIN hmi, UINT msg, DWORD_PTR inst,
                                   DWORD_PTR p1, DWORD_PTR p2);

    HMIDIIN      handle_ = nullptr;
    MidiCallback cb_;
};

// Print every MIDI input device winmm sees, to stdout.
void print_device_list();

} // namespace djmidi
