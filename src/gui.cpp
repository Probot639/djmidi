// WebView2-hosted GUI. The Win32 window is a shell; everything visible is
// rendered by Edge's Chromium engine loading index.html from the exe directory.
// JS calls into C++ through webview_bind, C++ pushes events into JS via
// webview_dispatch + webview_eval (see push_* and the bind_* callbacks below).

#include "gui.h"

#include "config.h"
#include "input.h"
#include "jog.h"
#include "state.h"

#include <windows.h>

// Only this TU includes webview.h since it carries its own implementation.
#include "webview.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace djmidi {

static webview_t           g_wv = nullptr;
static std::atomic<bool>   g_running{false};
static std::thread         g_worker;
static FILETIME            g_last_config_mtime{};
static std::atomic<int>    g_learn_state{0};    // 0 idle, 1 awaiting midi

// webview_dispatch takes a raw void* and a function pointer. We malloc the JS
// string, hand it off, and the dispatched lambda frees it. Ugly but it works
// and the alternative is a queue + condvar pair just to ferry strings.
static void eval_and_free(webview_t w, void* arg) {
    auto* js = static_cast<char*>(arg);
    webview_eval(w, js);
    std::free(js);
}

static void dispatch_eval(const std::string& js) {
    if (!g_wv) return;
    char* copy = static_cast<char*>(std::malloc(js.size() + 1));
    if (!copy) return;
    std::memcpy(copy, js.c_str(), js.size() + 1);
    webview_dispatch(g_wv, eval_and_free, copy);
}

static std::string js_quote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

static void push_status() {
    nlohmann::json j;
    {
        std::lock_guard<std::mutex> lk(state_mu);
        j["profile"]     = g_cfg.active_profile;
        j["config_path"] = g_config_path;
        j["device"]      = g_cfg.device_name_match;
        auto it = g_cfg.profiles.find(g_cfg.active_profile);
        if (it != g_cfg.profiles.end()) {
            j["notes"] = it->second.notes.size();
            j["ccs"]   = it->second.ccs.size();
        } else {
            j["notes"] = 0;
            j["ccs"]   = 0;
        }
    }
    dispatch_eval("window.djmidi&&window.djmidi.onStatus&&window.djmidi.onStatus(" + j.dump() + ");");
}

static void push_sys_log(const std::string& text) {
    dispatch_eval("window.djmidi&&window.djmidi.onSysLog&&window.djmidi.onSysLog(" + js_quote(text) + ");");
}

// Called from the winmm callback thread. Has to be cheap, no blocking.
void gui_post_midi(const MidiEvent& ev) {
    if (!g_wv) return;

    unsigned status = ev.status;
    unsigned type   = status & 0xF0;
    unsigned ch     = (status & 0x0F) + 1u;
    unsigned d1     = ev.data1;
    unsigned d2     = ev.data2;

    // First note-on (or any CC) during learn-armed becomes the capture target.
    if (g_learn_state.load() == 1) {
        const char* kind = nullptr;
        if (type == 0x90 && d2 > 0) kind = "note";
        else if (type == 0xB0)      kind = "cc";
        if (kind) {
            g_learn_state = 0;
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "window.djmidi&&window.djmidi.onLearnCapture&&window.djmidi.onLearnCapture(%u,%u,\"%s\");",
                ch, d1, kind);
            dispatch_eval(buf);
        }
    }

    char buf[160];
    std::snprintf(buf, sizeof(buf),
        "window.djmidi&&window.djmidi.onMidi&&window.djmidi.onMidi(%u,%u,%u,%u);",
        status, ch, d1, d2);
    dispatch_eval(buf);
}

// JS bridge handlers below. webview_bind exposes each as window.<name>(...)
// which returns a Promise; webview_return resolves it.

static void bind_app_ready(const char* seq, const char*, void*) {
    push_status();
    push_sys_log("gui ready");
    webview_return(g_wv, seq, 0, "true");
}

static void bind_reload(const char* seq, const char*, void*) {
    std::string err;
    bool ok = load_and_apply(g_config_path, err);
    if (ok) {
        push_status();
        push_sys_log("config reloaded");
        webview_return(g_wv, seq, 0, "true");
    } else {
        push_sys_log("reload failed: " + err);
        std::string j = js_quote(err);
        webview_return(g_wv, seq, 1, j.c_str());
    }
}

static void bind_start_learn(const char* seq, const char*, void*) {
    g_learn_state = 1;
    push_sys_log("learn armed, hit a pad");
    webview_return(g_wv, seq, 0, "true");
}

static void bind_cancel_learn(const char* seq, const char*, void*) {
    g_learn_state = 0;
    push_sys_log("learn cancelled");
    webview_return(g_wv, seq, 0, "true");
}

// JS args arrive as a JSON array: [ch, d1, kind, vk_string].
static void bind_save_binding(const char* seq, const char* req, void*) {
    nlohmann::json args = nlohmann::json::parse(req, nullptr, false);
    if (!args.is_array() || args.size() != 4) {
        webview_return(g_wv, seq, 1, "\"invalid args\"");
        return;
    }
    unsigned    ch     = args[0].get<unsigned>();
    unsigned    d1     = args[1].get<unsigned>();
    std::string kind   = args[2].get<std::string>();
    std::string vk_str = args[3].get<std::string>();

    WORD vk = vk_from_string(vk_str);
    if (vk == 0) {
        webview_return(g_wv, seq, 1, js_quote("unknown VK: " + vk_str).c_str());
        return;
    }

    Action a;
    a.type = ActionType::KeyTap;
    a.key  = vk;
    uint16_t k = bind_key(ch, d1);

    {
        std::lock_guard<std::mutex> lk(state_mu);
        auto it = g_cfg.profiles.find(g_cfg.active_profile);
        if (it == g_cfg.profiles.end()) {
            webview_return(g_wv, seq, 1, "\"no active profile\"");
            return;
        }
        if (kind == "note") it->second.notes[k] = a;
        else                it->second.ccs[k]   = a;
    }

    std::string err;
    bool ok;
    {
        std::lock_guard<std::mutex> lk(state_mu);
        ok = save_config(g_config_path, g_cfg, err);
    }

    if (ok) {
        // Bump our copy of the mtime so the watcher doesn't double-reload us.
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (GetFileAttributesExA(g_config_path.c_str(), GetFileExInfoStandard, &data))
            g_last_config_mtime = data.ftLastWriteTime;

        push_status();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "bound %u:0x%02X", ch, d1);
        push_sys_log(buf);
        webview_return(g_wv, seq, 0, "true");
    } else {
        webview_return(g_wv, seq, 1, js_quote(err).c_str());
    }
}

static void check_config_mtime() {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(g_config_path.c_str(), GetFileExInfoStandard, &data))
        return;
    if (CompareFileTime(&data.ftLastWriteTime, &g_last_config_mtime) == 0)
        return;

    g_last_config_mtime = data.ftLastWriteTime;
    std::string err;
    if (load_and_apply(g_config_path, err)) {
        push_status();
        push_sys_log("config reloaded (external edit)");
    } else {
        push_sys_log("reload failed: " + err);
    }
}

// Single background thread does both jog idle-release and config-file polling.
// Started after gui_run sets up the webview, joined on shutdown.
static void worker_loop() {
    using clock = std::chrono::steady_clock;
    auto next_jog   = clock::now();
    auto next_mtime = next_jog;

    while (g_running.load()) {
        auto now = clock::now();
        if (now >= next_jog) {
            {
                std::lock_guard<std::mutex> lk(state_mu);
                for (auto& kv : g_jogs) kv.second->poll();
            }
            next_jog = now + std::chrono::milliseconds(20);
        }
        if (now >= next_mtime) {
            check_config_mtime();
            next_mtime = now + std::chrono::milliseconds(500);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

static std::string resolve_index_html_url() {
    char exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::string p(exe_path, n);
    size_t slash = p.find_last_of("\\/");
    if (slash != std::string::npos) p.resize(slash + 1);
    p += "index.html";
    for (auto& c : p) if (c == '\\') c = '/';
    return "file:///" + p;
}

int gui_run(HINSTANCE /*hInst*/) {
    g_wv = webview_create(0, nullptr);
    if (!g_wv) {
        std::fprintf(stderr,
            "webview_create failed. WebView2 runtime missing? "
            "It ships with Win11; on Win10 install Microsoft's Evergreen redist.\n");
        return 5;
    }

    webview_set_title(g_wv, "djmidi");
    webview_set_size(g_wv, 1280, 820, WEBVIEW_HINT_NONE);

    webview_bind(g_wv, "app_ready",    bind_app_ready,    nullptr);
    webview_bind(g_wv, "reload",       bind_reload,       nullptr);
    webview_bind(g_wv, "start_learn",  bind_start_learn,  nullptr);
    webview_bind(g_wv, "cancel_learn", bind_cancel_learn, nullptr);
    webview_bind(g_wv, "save_binding", bind_save_binding, nullptr);

    std::string url = resolve_index_html_url();
    std::printf("gui: loading %s\n", url.c_str());
    std::fflush(stdout);
    webview_navigate(g_wv, url.c_str());

    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExA(g_config_path.c_str(), GetFileExInfoStandard, &data))
        g_last_config_mtime = data.ftLastWriteTime;

    g_running = true;
    g_worker  = std::thread(worker_loop);

    webview_run(g_wv);

    g_running = false;
    if (g_worker.joinable()) g_worker.join();
    webview_destroy(g_wv);
    g_wv = nullptr;
    return 0;
}

} // namespace djmidi
