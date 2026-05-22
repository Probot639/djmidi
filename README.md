# djmidi

## Why

There are a few projects that exist for rebinding midi input to a normal keyboard/mouse setup, however none out there fit my use case of a DDJ-FLX4 so after discussing this with the girl i'm currently seeing i just decided to build it myself now. This project basically only exists because I want to play Trackmania 2020 and EX-XDRiVER using my dj decks.

## Project Description

Pioneer DDJ-FLX4 to Windows keyboard. MIDI input from the controller gets routed to `SendInput` keystrokes and mouse deltas so the FLX4 can drive games. Personal project. There is no public FLX4 MIDI map worth trusting end-to-end, so step one is the sniffer.

## Build

You need MinGW-w64. From WSL/Debian/Ubuntu:

```
sudo apt install mingw-w64
make
```

Output goes to `build/sniff.exe` and `build/djmidi.exe`. The repo lives on `/mnt/e/djmidi` so the exes are reachable from Windows at `E:\djmidi\build\`.

## Sniffer usage

```
> build\sniff.exe
found 1 MIDI input device(s)
  [0] DDJ-FLX4

usage: sniff <name-substring>   or   sniff -i <device-id>

> build\sniff.exe FLX4
opening device 0. ctrl+c to quit.
[   12345 ms] CH1  NOTEON  D1=0x0B ( 11)  D2=127
[   12389 ms] CH1  NOTEOF  D1=0x0B ( 11)  D2=  0
[   13002 ms] CH1  CC      D1=0x1F ( 31)  D2= 64
...
```

D1 is data byte 1 (note number or CC number), D2 is the value. Channel is 1-indexed because that's how Pioneer / Serato / rekordbox all number them in their docs.

Press every pad, twist every knob, scroll both jog wheels with the controller plugged in. Save the output. That's the empirical MIDI map.

## Status

- [x] MIDI sniffer
- [x] SendInput wrapper (keys via scan codes, mouse delta, mouse buttons, mouse wheel)
- [x] v0 main with hardcoded FLX4 mapping
- [x] All three jog modes (held key, mouse XY, tap-per-N) in `src/jog.{h,cpp}`
- [x] All three knob modes (wheel scroll, threshold, direction tap) in `src/knob.{h,cpp}`
- [x] JSON config with hot reload (`config/default.json`, polled every 500ms)
- [x] WebView2 GUI hosted inside the exe: visual rig, live event readout, reload, MIDI learn

## Running

```
make
```

Run from the project root so the default config path resolves:

```
> cd E:\djmidi
> build\djmidi.exe
```

Pass a path to override:

```
> build\djmidi.exe config\elden-ring.json
```

Edit the config file while djmidi is running, it'll reload the mapping
within 500ms. Bad JSON keeps the previous config and prints the error.

Default mapping (in `config/default.json`, testable in Notepad):

| Control                       | Action               |
|-------------------------------|----------------------|
| Deck 1 hot cue pads           | 1..8                 |
| Deck 2 hot cue pads           | A..H                 |
| Deck 1 Play / Cue / Sync      | Space / Enter / Backspace |
| Deck 2 Play / Cue / Sync      | Tab / Escape / Delete |
| Deck 1 platter touch          | Right mouse (hold)   |
| Deck 2 platter touch          | Left  mouse (hold)   |
| Deck 1 jog wheel rotation     | Mouse X delta (MouseX mode) |
| Deck 2 jog wheel rotation     | A held (CCW) / D held (CW), released when wheel stops (HeldKey mode) |
| Deck 1 channel fader          | S held (bottom) / W held (top), middle = nothing (Knob threshold) |
| Deck 1 filter knob            | Mouse wheel scroll (Knob wheel_scroll) |
| Deck 1 EQ Hi knob             | Up / Down arrow taps (Knob direction_tap) |

## GUI

`build\djmidi.exe` opens a WebView2 window. Edge's Chromium engine renders `index.html` from the build directory, so the layout is HTML/CSS, easy to tweak without recompiling C++. WebView2 ships with Windows 11; on Windows 10 install Microsoft's Edge WebView2 Evergreen runtime.

Inside the window:

- Visual rig: deck 1, deck 2, mixer. Controls flash when their MIDI event fires.
- Status block (profile name, config path, binding counts).
- Live readout, last 250 events, scrolls inside its panel.
- **Reload config** button, calls back into C++ to re-read the JSON.
- **Start MIDI learn**. Click it, hit any pad/button/knob on the FLX4. C++ catches the event, sends the (ch, d1, kind) back to the page, focus jumps to the key box. Press a key and the binding is written to JSON. The mtime watcher reloads automatically.

Profile switching is not in the UI yet, edit `active_profile` in the JSON and save, the running app picks it up.

The HTML lives at `frontend_template/index.html`, the Makefile copies it to `build/index.html` next to the exe. Edit the HTML and re-`make` to refresh the copy, then relaunch djmidi.exe.

## Config format

`config/default.json` is the canonical example. Top-level shape:

```json
{
  "device_name_match": "FLX4",
  "active_profile":    "default",
  "profiles": {
    "default": {
      "description": "...",
      "notes": { "<channel>:<note>": <action>, ... },
      "ccs":   { "<channel>:<cc>":   <action>, ... }
    }
  }
}
```

Channel is 1-indexed (1..16). Note/CC accepts decimal (`11`) or hex
(`"0x0B"`).

Action types:

```json
{ "type": "key_tap",      "key": "VK_SPACE" }
{ "type": "key_hold",     "key": "A" }              // pressed while note held
{ "type": "mouse_button", "button": "left|right|middle" }
{ "type": "jog",
  "mode": "mouse_x" | "mouse_y" | "held_key" | "tap_per_n",
  "deadzone": 1,            // |value-64| <= this is dropped
  "mouse_scale": 5,         // mouse modes: pixels per (value-64)
  "key_left":  "A",         // held_key mode
  "key_right": "D",
  "hold_threshold":  2,     // |delta| >= this triggers the held key
  "hold_timeout_ms": 120,   // release after this many ms of silence
  "tap_left":  "VK_LEFT",   // tap_per_n mode
  "tap_right": "VK_RIGHT",
  "ticks_per_tap": 4
}

{ "type": "knob",
  "mode": "wheel_scroll" | "threshold" | "direction_tap",
  // wheel_scroll:
  "units_per_tick": 4,      // change in value per mouse-wheel notch
  "invert": false,
  // threshold (faders/sliders):
  "low_threshold":  30,     // value < this -> key_low held
  "high_threshold": 90,     // value > this -> key_high held
  "key_low":  "S",
  "key_high": "W",
  // direction_tap (rotary or fader, fires on change):
  "key_inc": "VK_UP",       // tap when value increases by N units
  "key_dec": "VK_DOWN",
  "units_per_tap": 6
}
```

Jog and Knob both bind to CC events. The difference: **jog** expects relative deltas around 64 (FLX4 wheel encoder behavior); **knob** reads absolute 0..127 values and tracks change internally. Bind a wheel CC as `jog`, bind a fader/knob CC as `knob`.

`key` strings accept the `VK_*` constant name, the short alias (`SPACE`, `ENTER`, `LSHIFT`, `F1`), or single letters/digits (`A`, `7`).

## FLX4 MIDI map (empirical, derived from sniffer)

Channels are 1-indexed (Pioneer convention).

| Control                         | Channel | Message                 |
|---------------------------------|---------|-------------------------|
| Play                            | 1 / 2   | Note 0x0B               |
| Cue                             | 1 / 2   | Note 0x0C               |
| Sync                            | 1 / 2   | Note 0x58               |
| Pad-mode buttons                | 1 / 2   | Note 0x1B, 0x1E, 0x20, 0x22 |
| Jog platter touch               | 1 / 2   | Note 0x36               |
| Loop / beat-loop buttons        | 1 / 2   | Note 0x4D, 0x51, 0x53    |
| Shift / unidentified            | 1 / 2   | Note 0x3F, 0x54 (TBD)    |
| Jog wheel spin (relative ±64)   | 1 / 2   | CC 0x21                 |
| Jog scratch (only when touched) | 1 / 2   | CC 0x22                 |
| Tempo slider (14-bit)           | 1 / 2   | CC 0x00 MSB + 0x20 LSB  |
| Filter knob (14-bit)            | 1 / 2   | CC 0x04 MSB + 0x24 LSB  |
| Channel fader (14-bit)          | 1 / 2   | CC 0x07 MSB + 0x27 LSB  |
| EQ Low (14-bit)                 | 1 / 2   | CC 0x0B MSB + 0x2B LSB  |
| EQ Mid (14-bit)                 | 1 / 2   | CC 0x0F MSB + 0x2F LSB  |
| EQ Hi  (14-bit)                 | 1 / 2   | CC 0x13 MSB + 0x33 LSB  |
| Hot cue pads                    | 8 / 10  | Note 0x00..0x07         |
| Pad-mode pads (other modes)     | 8 / 10  | Note 0x22..0x27, etc.   |
| Mixer FX / Smart CFX section    | 7       | various Note + CC pairs |
| Crossfader (14-bit)             | 5       | CC 0x02 MSB + 0x22 LSB  |
| Browser knob / master section   | 5       | various                 |

Note the jog wheel quirk: CC 0x21 reports relative deltas around a center of 64 (so 65 = one tick forward, 63 = one tick back). The wheel emits spurious 63/65 events when sitting still, so the engine applies a small deadzone before forwarding anything.

## Anti-cheat note

SendInput is fine for normal games, emulators, racing sims, rhythm games. Anti-cheat layers like Vanguard or Faceit's client block SendInput entirely. If you ever want those, the injection layer would need to swap to the Interception driver, which is a contained change inside `input.cpp`.

## Possible Features
- these all rely on me actually wanting to continue this once i get the basic functionality working
- Better UI
- More controller support
  - More input support
  - Default profiles for controllers and games
- Pushing to this repo more than 3 times
