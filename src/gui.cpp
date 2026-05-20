#include "gui.h"

#include "config.h"
#include "input.h"
#include "jog.h"
#include "state.h"

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace djmidi {

// ------------------------------------------------------------------
// Win32 globals
// ------------------------------------------------------------------

#define WM_APP_MIDI_EVENT  (WM_APP + 1)

#define ID_BTN_RELOAD     1001
#define ID_BTN_LEARN      1002
#define ID_TIMER_POLL     1
#define ID_TIMER_RELOAD   2
#define ID_LAST_EVENT     1010
#define ID_LEARN_STATUS   1011

static HWND     hMain        = nullptr;
static HWND     hStatusEdit  = nullptr;
static HWND     hLastEvent   = nullptr;
static HWND     hLearnStatus = nullptr;
static HWND     hLearnBtn    = nullptr;
static HWND     hReloadBtn   = nullptr;
static HWND     hKeyEdit     = nullptr;
static WNDPROC  orig_keyedit = nullptr;

static FILETIME g_last_config_mtime{};

enum class LearnState { Idle, AwaitingMidi, AwaitingKey };
static LearnState learn_state = LearnState::Idle;
static uint8_t    learn_ch    = 0;
static uint8_t    learn_type  = 0;   // 0x90 or 0xB0
static uint8_t    learn_num   = 0;

// ------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------

static void set_text(HWND h, const std::string& s) {
    if (h) SetWindowTextA(h, s.c_str());
}

static void update_status() {
    std::lock_guard<std::mutex> lk(state_mu);
    std::string s = "Profile : " + g_cfg.active_profile + "\r\n";
    s            += "Config  : " + g_config_path        + "\r\n";

    auto it = g_cfg.profiles.find(g_cfg.active_profile);
    if (it != g_cfg.profiles.end()) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "Bindings: %zu notes, %zu CCs\r\n",
            it->second.notes.size(), it->second.ccs.size());
        s += buf;
    }
    set_text(hStatusEdit, s);
}

// ------------------------------------------------------------------
// MIDI -> GUI delivery (called from winmm callback thread)
// ------------------------------------------------------------------

void gui_post_midi(const MidiEvent& ev) {
    if (!hMain) return;
    // Pack the three bytes into LPARAM. Timestamp not needed in GUI.
    LPARAM lp = (LPARAM(ev.status) << 16) | (LPARAM(ev.data1) << 8) | LPARAM(ev.data2);
    PostMessageA(hMain, WM_APP_MIDI_EVENT, 0, lp);
}

// ------------------------------------------------------------------
// MIDI learn flow
// ------------------------------------------------------------------

static LRESULT CALLBACK keyedit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

static void cancel_learn() {
    learn_state = LearnState::Idle;
    set_text(hLearnStatus, "idle");
    SetWindowTextA(hLearnBtn, "Start MIDI learn");
    EnableWindow(hKeyEdit, FALSE);
    SetWindowTextA(hKeyEdit, "");
    if (orig_keyedit) {
        SetWindowLongPtrA(hKeyEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig_keyedit));
        orig_keyedit = nullptr;
    }
}

static void enter_learn() {
    learn_state = LearnState::AwaitingMidi;
    set_text(hLearnStatus, "hit any pad, button, or knob on the FLX4...");
    SetWindowTextA(hLearnBtn, "Cancel learn");
}

static void on_midi_for_learn(uint8_t status, uint8_t d1, uint8_t d2) {
    if (learn_state != LearnState::AwaitingMidi) return;
    uint8_t type = status & 0xF0;
    if (type != 0x90 && type != 0xB0) return;
    if (type == 0x90 && d2 == 0)      return;   // only the press, not release

    learn_ch   = (status & 0x0F) + 1u;
    learn_type = type;
    learn_num  = d1;
    learn_state = LearnState::AwaitingKey;

    char buf[160];
    std::snprintf(buf, sizeof(buf),
        "captured %u:0x%02X (%s). click the key box and press a key.",
        learn_ch, learn_num, (type == 0x90) ? "note" : "cc");
    set_text(hLearnStatus, buf);

    EnableWindow(hKeyEdit, TRUE);
    SetFocus(hKeyEdit);
    // Subclass the edit so we intercept WM_KEYDOWN with the full VK code,
    // not just the character that the edit would normally absorb.
    orig_keyedit = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(hKeyEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(keyedit_proc)));
}

static void apply_learned_key(WORD vk) {
    Action a;
    a.type = ActionType::KeyTap;
    a.key  = vk;

    uint16_t k = bind_key(learn_ch, learn_num);

    {
        std::lock_guard<std::mutex> lk(state_mu);
        auto it = g_cfg.profiles.find(g_cfg.active_profile);
        if (it == g_cfg.profiles.end()) {
            set_text(hLearnStatus, "active profile vanished during learn?? bailing");
            cancel_learn();
            return;
        }
        if (learn_type == 0x90) {
            it->second.notes[k] = a;
        } else {
            // CC -> key_tap is an unusual binding but allowed. CC bindings
            // are normally Jog actions; a key_tap on a CC fires every time
            // the CC changes value, which is *a lot* during a knob sweep.
            it->second.ccs[k] = a;
        }
    }

    std::string err;
    bool ok;
    {
        std::lock_guard<std::mutex> lk(state_mu);
        ok = save_config(g_config_path, g_cfg, err);
    }

    if (ok) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "bound %u:0x%02X -> %s. saved.",
            learn_ch, learn_num, vk_to_string(vk).c_str());
        set_text(hLearnStatus, buf);
        // The mtime watcher will pick this up on its next tick and reload.
    } else {
        std::string msg = "SAVE FAILED: " + err;
        set_text(hLearnStatus, msg);
    }

    cancel_learn();
    update_status();
}

static LRESULT CALLBACK keyedit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && learn_state == LearnState::AwaitingKey) {
        WORD vk = static_cast<WORD>(wp);
        // Skip pure modifier presses so the user can hold shift to type
        // shifted symbols without it being captured as the bind key.
        if (vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU &&
            vk != VK_LSHIFT && vk != VK_RSHIFT &&
            vk != VK_LCONTROL && vk != VK_RCONTROL &&
            vk != VK_LMENU && vk != VK_RMENU)
        {
            apply_learned_key(vk);
            return 0;   // eat the keystroke; don't insert into the edit
        }
    }
    return CallWindowProc(orig_keyedit, h, msg, wp, lp);
}

// ------------------------------------------------------------------
// Window construction + main proc
// ------------------------------------------------------------------

static void create_children(HWND parent, HINSTANCE hInst) {
    int y = 10;
    auto label = [&](const char* txt, int h = 18) {
        CreateWindowExA(0, "STATIC", txt, WS_CHILD | WS_VISIBLE,
            10, y, 480, h, parent, NULL, hInst, NULL);
        y += h + 4;
    };

    label("djmidi v0.7 — FLX4 -> keyboard / mouse");
    y += 6;

    hStatusEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY,
        10, y, 480, 70, parent, NULL, hInst, NULL);
    y += 78;

    label("last MIDI event:");
    hLastEvent = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "(none yet)",
        WS_CHILD | WS_VISIBLE | ES_READONLY,
        10, y, 480, 22, parent, reinterpret_cast<HMENU>(ID_LAST_EVENT), hInst, NULL);
    y += 30;

    hReloadBtn = CreateWindowExA(0, "BUTTON", "Reload config",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, y, 140, 28, parent, reinterpret_cast<HMENU>(ID_BTN_RELOAD), hInst, NULL);

    hLearnBtn = CreateWindowExA(0, "BUTTON", "Start MIDI learn",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        160, y, 160, 28, parent, reinterpret_cast<HMENU>(ID_BTN_LEARN), hInst, NULL);
    y += 36;

    label("learn status:");
    hLearnStatus = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "idle",
        WS_CHILD | WS_VISIBLE | ES_READONLY,
        10, y, 480, 22, parent, reinterpret_cast<HMENU>(ID_LEARN_STATUS), hInst, NULL);
    y += 30;

    label("key input (during learn, press the key you want to bind):");
    hKeyEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        10, y, 200, 22, parent, NULL, hInst, NULL);
    EnableWindow(hKeyEdit, FALSE);
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
        std::printf("[reload] config updated, profile=%s\n", g_cfg.active_profile.c_str());
        update_status();
    } else {
        std::printf("[reload] FAILED: %s\n", err.c_str());
    }
    std::fflush(stdout);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        create_children(h, cs->hInstance);
        update_status();
        SetTimer(h, ID_TIMER_POLL,   20,  NULL);
        SetTimer(h, ID_TIMER_RELOAD, 500, NULL);
        return 0;
    }

    case WM_COMMAND: {
        WORD id   = LOWORD(wp);
        WORD code = HIWORD(wp);
        if (id == ID_BTN_RELOAD && code == BN_CLICKED) {
            std::string err;
            if (load_and_apply(g_config_path, err)) {
                set_text(hLearnStatus, "config reloaded");
            } else {
                set_text(hLearnStatus, "reload failed: " + err);
            }
            update_status();
            return 0;
        }
        if (id == ID_BTN_LEARN && code == BN_CLICKED) {
            if (learn_state == LearnState::Idle) enter_learn();
            else                                 cancel_learn();
            return 0;
        }
        return 0;
    }

    case WM_APP_MIDI_EVENT: {
        uint8_t status = (lp >> 16) & 0xFF;
        uint8_t d1     = (lp >> 8)  & 0xFF;
        uint8_t d2     =  lp        & 0xFF;

        const char* tn = "?";
        switch (status & 0xF0) {
            case 0x80: tn = "NOTEOFF"; break;
            case 0x90: tn = "NOTEON";  break;
            case 0xB0: tn = "CC";      break;
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "ch=%u  %-7s  d1=0x%02X (%3u)  d2=%3u",
            (status & 0x0F) + 1u, tn, d1, d1, d2);
        SetWindowTextA(hLastEvent, buf);

        if (learn_state == LearnState::AwaitingMidi)
            on_midi_for_learn(status, d1, d2);
        return 0;
    }

    case WM_TIMER:
        if (wp == ID_TIMER_POLL) {
            std::lock_guard<std::mutex> lk(state_mu);
            for (auto& kv : g_jogs) kv.second->poll();
        } else if (wp == ID_TIMER_RELOAD) {
            check_config_mtime();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(h, ID_TIMER_POLL);
        KillTimer(h, ID_TIMER_RELOAD);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int gui_run(HINSTANCE hInst) {
    WNDCLASSA wc{};
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = "djmidi-main";
    if (!RegisterClassA(&wc)) return 4;

    hMain = CreateWindowExA(0, "djmidi-main", "djmidi",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 380,
        NULL, NULL, hInst, NULL);
    if (!hMain) return 5;

    // Init mtime tracker so the first 500ms tick doesn't spuriously reload.
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExA(g_config_path.c_str(), GetFileExInfoStandard, &data))
        g_last_config_mtime = data.ftLastWriteTime;

    ShowWindow(hMain, SW_SHOW);
    UpdateWindow(hMain);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

} // namespace djmidi
