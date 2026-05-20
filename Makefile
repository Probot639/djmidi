# djmidi — MinGW-w64 cross-compile from WSL.
# tested on debian/ubuntu with `apt install mingw-w64`.

CXX      := x86_64-w64-mingw32-g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc -Ithird_party
LDFLAGS  := -lwinmm -lgdi32 -luser32 -static-libgcc -static-libstdc++ -static

BUILD := build
JSON_URL := https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
JSON_HDR := third_party/nlohmann/json.hpp

.PHONY: all clean sniff djmidi

all: $(BUILD)/sniff.exe $(BUILD)/djmidi.exe

sniff:  $(BUILD)/sniff.exe
djmidi: $(BUILD)/djmidi.exe

# Fetch the single-header JSON lib on demand. ~1MB, one HTTP GET.
$(JSON_HDR):
	mkdir -p $(dir $@)
	curl -fL $(JSON_URL) -o $@

$(BUILD)/sniff.exe: src/sniff.cpp src/midi.cpp src/midi.h | $(BUILD)
	$(CXX) $(CXXFLAGS) src/sniff.cpp src/midi.cpp -o $@ $(LDFLAGS)

$(BUILD)/djmidi.exe: \
		src/main.cpp \
		src/midi.cpp   src/midi.h    \
		src/input.cpp  src/input.h   \
		src/jog.cpp    src/jog.h     \
		src/knob.cpp   src/knob.h    \
		src/config.cpp src/config.h  \
		src/gui.cpp    src/gui.h     \
		src/state.h                  \
		$(JSON_HDR) | $(BUILD)
	$(CXX) $(CXXFLAGS) \
		src/main.cpp src/midi.cpp src/input.cpp src/jog.cpp src/knob.cpp \
		src/config.cpp src/gui.cpp \
		-o $@ $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
