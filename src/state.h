// Shared globals between main.cpp and gui.cpp. main.cpp defines them.

#pragma once

#include "config.h"
#include "jog.h"
#include "knob.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace djmidi {

extern std::mutex                                          state_mu;
extern Config                                              g_cfg;
extern std::unordered_map<uint16_t, std::unique_ptr<Jog>>  g_jogs;
extern std::unordered_map<uint16_t, std::unique_ptr<Knob>> g_knobs;
extern std::string                                         g_config_path;

bool load_and_apply(const std::string& path, std::string& err);

} // namespace djmidi
