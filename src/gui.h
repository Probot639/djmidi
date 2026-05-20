// Minimal Win32 GUI for djmidi.
//
// Layout (no fancy controls — labels, edit boxes, buttons):
//   - status block: device, profile, binding count
//   - last MIDI event readout (live)
//   - Reload button
//   - MIDI learn flow: click "Learn", hit a pad, focus the key box,
//     press a key. The new key_tap binding is appended to the active
//     profile and written back to the JSON file. The mtime watcher
//     then re-applies it.

#pragma once

#include <windows.h>
#include "midi.h"

namespace djmidi {

// Push a MIDI event to the GUI thread for display. Called from the
// winmm callback thread; uses PostMessage so it never blocks.
void gui_post_midi(const MidiEvent& ev);

// Runs the Win32 message loop. Blocks until the window is closed.
int gui_run(HINSTANCE hInst);

} // namespace djmidi
