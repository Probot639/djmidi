#include "midi.h"

#include <cstdio>
#include <utility>

namespace djmidi {

MidiInput::~MidiInput() {
    close();
}

void CALLBACK MidiInput::midi_proc(HMIDIIN, UINT msg, DWORD_PTR inst,
                                   DWORD_PTR p1, DWORD_PTR p2) {
    if (msg != MIM_DATA) return;  // ignore MIM_LONGDATA / MIM_OPEN / MIM_CLOSE for now

    auto* self = reinterpret_cast<MidiInput*>(inst);
    if (!self || !self->cb_) return;

    DWORD packed = static_cast<DWORD>(p1);
    MidiEvent ev{};
    ev.status = packed         & 0xFF;
    ev.data1  = (packed >>  8) & 0xFF;
    ev.data2  = (packed >> 16) & 0xFF;
    ev.ts_ms  = static_cast<DWORD>(p2);
    self->cb_(ev);
}

int MidiInput::find_device(const std::string& name_substr) {
    UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSA caps{};
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
        std::string name = caps.szPname;
        if (name.find(name_substr) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool MidiInput::open(int dev_id, MidiCallback cb) {
    if (handle_) close();
    cb_ = std::move(cb);

    MMRESULT r = midiInOpen(&handle_, static_cast<UINT>(dev_id),
                            reinterpret_cast<DWORD_PTR>(&midi_proc),
                            reinterpret_cast<DWORD_PTR>(this),
                            CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR) {
        handle_ = nullptr;
        return false;
    }
    midiInStart(handle_);
    return true;
}

void MidiInput::close() {
    if (!handle_) return;
    midiInStop(handle_);
    midiInReset(handle_);   // flushes any pending MIM_LONGDATA buffers
    midiInClose(handle_);
    handle_ = nullptr;
}

void print_device_list() {
    UINT n = midiInGetNumDevs();
    std::printf("found %u MIDI input device(s)\n", n);
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSA caps{};
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) {
            std::printf("  [%u] <error reading caps>\n", i);
            continue;
        }
        std::printf("  [%u] %s\n", i, caps.szPname);
    }
}

} // namespace djmidi
