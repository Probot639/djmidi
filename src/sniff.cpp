// MIDI sniffer. Dumps every event from the FLX4 (or any matching device)
// so you can map controls empirically — Pioneer's published MIDI table
// for the FLX4 is incomplete and a few control numbers are wrong.
//
// Usage:
//   sniff                  list MIDI input devices
//   sniff FLX4             open the first device whose name contains "FLX4"
//   sniff -i 2             open device id 2 directly

#include "midi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace djmidi;

static const char* status_name(uint8_t status) {
    switch (status & 0xF0) {
        case 0x80: return "NOTEOF";
        case 0x90: return "NOTEON";
        case 0xA0: return "PRSAFT";
        case 0xB0: return "CC    ";
        case 0xC0: return "PRGCHG";
        case 0xD0: return "CHNAFT";
        case 0xE0: return "PBEND ";
        case 0xF0: return "SYSEX ";
        default:   return "?     ";
    }
}

int main(int argc, char** argv) {
    if (argc == 1) {
        print_device_list();
        std::printf("\nusage: sniff <name-substring>   or   sniff -i <device-id>\n");
        return 0;
    }

    MidiInput mi;
    int dev_id = -1;

    if (argc >= 3 && std::strcmp(argv[1], "-i") == 0) {
        dev_id = std::atoi(argv[2]);
    } else {
        dev_id = mi.find_device(argv[1]);
        if (dev_id < 0) {
            std::fprintf(stderr, "no device matching '%s'\n", argv[1]);
            print_device_list();
            return 1;
        }
    }

    std::printf("opening device %d. ctrl+c to quit.\n", dev_id);

    bool ok = mi.open(dev_id, [](const MidiEvent& ev) {
        unsigned ch = (ev.status & 0x0F) + 1;   // DJ-controller docs are 1-indexed
        std::printf("[%8u ms] CH%-2u %s D1=0x%02X (%3u)  D2=%3u\n",
                    (unsigned)ev.ts_ms, ch, status_name(ev.status),
                    ev.data1, ev.data1, ev.data2);
        std::fflush(stdout);
    });

    if (!ok) {
        std::fprintf(stderr, "midiInOpen failed for device %d\n", dev_id);
        return 2;
    }

    // winmm runs the callback on its own worker thread, so main just parks.
    // No clean shutdown path — user hits ctrl+c, the process dies, Windows
    // reclaims the handle. Fine for a sniffer.
    while (true) Sleep(60000);
}
