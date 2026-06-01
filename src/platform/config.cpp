#include "config.h"
#include "core/app_state.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace app {

static std::wstring config_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    wchar_t* last_slash = wcsrchr(buf, L'\\');
    if (last_slash) *(last_slash + 1) = L'\0';
    wcscat_s(buf, L"config.ini");
    return buf;
}

static const wchar_t* SEC_PARAMS  = L"params";
static const wchar_t* SEC_HOTKEY  = L"hotkey";

void load_config() {
    const std::wstring path = config_path();
    const wchar_t* p = path.c_str();

    param_loop_speed_ms  .store(static_cast<int>(GetPrivateProfileIntW(SEC_PARAMS, L"loop_speed_ms",   500, p)));
    param_jitter_ms      .store(static_cast<int>(GetPrivateProfileIntW(SEC_PARAMS, L"jitter_ms",       100, p)));
    param_press_base_ms  .store(static_cast<int>(GetPrivateProfileIntW(SEC_PARAMS, L"press_base_ms",    10, p)));
    param_press_jitter_ms.store(static_cast<int>(GetPrivateProfileIntW(SEC_PARAMS, L"press_jitter_ms",  10, p)));
    param_clicks_per_rest.store(static_cast<int>(GetPrivateProfileIntW(SEC_PARAMS, L"clicks_per_rest",   0, p)));
    param_rest_seconds   .store(static_cast<int>(GetPrivateProfileIntW(SEC_PARAMS, L"rest_seconds",       3, p)));

    HotkeyConfig hk;
    hk.mouse_btn3 = GetPrivateProfileIntW(SEC_HOTKEY, L"mouse_btn3", 1, p) != 0;
    hk.mouse_btn4 = GetPrivateProfileIntW(SEC_HOTKEY, L"mouse_btn4", 0, p) != 0;
    hk.mouse_btn5 = GetPrivateProfileIntW(SEC_HOTKEY, L"mouse_btn5", 0, p) != 0;
    hk.key_ctrl   = GetPrivateProfileIntW(SEC_HOTKEY, L"key_ctrl",   0, p) != 0;
    hk.key_shift  = GetPrivateProfileIntW(SEC_HOTKEY, L"key_shift",  0, p) != 0;
    hk.key_alt    = GetPrivateProfileIntW(SEC_HOTKEY, L"key_alt",    0, p) != 0;
    {
        static const wchar_t* vkey_keys[4] = {L"vkey0", L"vkey1", L"vkey2", L"vkey3"};
        for (int i = 0; i < 4; ++i)
            hk.vkeys[i] = static_cast<uint8_t>(GetPrivateProfileIntW(SEC_HOTKEY, vkey_keys[i], 0, p));
    }
    { std::lock_guard<std::mutex> lk(g_hotkey_mutex); g_hotkey = hk; }
}

void save_config() {
    const std::wstring path = config_path();
    const wchar_t* p = path.c_str();

    auto write = [&](const wchar_t* sec, const wchar_t* key, int val) {
        wchar_t buf[32];
        _itow_s(val, buf, 10);
        WritePrivateProfileStringW(sec, key, buf, p);
    };

    write(SEC_PARAMS, L"loop_speed_ms",   param_loop_speed_ms  .load());
    write(SEC_PARAMS, L"jitter_ms",       param_jitter_ms      .load());
    write(SEC_PARAMS, L"press_base_ms",   param_press_base_ms  .load());
    write(SEC_PARAMS, L"press_jitter_ms", param_press_jitter_ms.load());
    write(SEC_PARAMS, L"clicks_per_rest", param_clicks_per_rest.load());
    write(SEC_PARAMS, L"rest_seconds",    param_rest_seconds   .load());

    HotkeyConfig hk;
    { std::lock_guard<std::mutex> lk(g_hotkey_mutex); hk = g_hotkey; }
    write(SEC_HOTKEY, L"mouse_btn3", hk.mouse_btn3 ? 1 : 0);
    write(SEC_HOTKEY, L"mouse_btn4", hk.mouse_btn4 ? 1 : 0);
    write(SEC_HOTKEY, L"mouse_btn5", hk.mouse_btn5 ? 1 : 0);
    write(SEC_HOTKEY, L"key_ctrl",   hk.key_ctrl   ? 1 : 0);
    write(SEC_HOTKEY, L"key_shift",  hk.key_shift  ? 1 : 0);
    write(SEC_HOTKEY, L"key_alt",    hk.key_alt    ? 1 : 0);
    {
        static const wchar_t* vkey_keys[4] = {L"vkey0", L"vkey1", L"vkey2", L"vkey3"};
        for (int i = 0; i < 4; ++i)
            write(SEC_HOTKEY, vkey_keys[i], hk.vkeys[i]);
    }
}

} // namespace app
