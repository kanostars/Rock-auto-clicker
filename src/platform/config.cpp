#include "config.h"
#include "core/app_state.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace app {

// 返回 exe 同目录下的 config.ini 的绝对路径
static std::wstring config_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    // 把文件名部分替换成 config.ini
    wchar_t* last_slash = wcsrchr(buf, L'\\');
    if (last_slash) *(last_slash + 1) = L'\0';
    wcscat_s(buf, L"config.ini");
    return buf;
}

static const wchar_t* SEC = L"params";

void load_config() {
    const std::wstring path = config_path();
    const wchar_t* p = path.c_str();

    param_loop_speed_ms  .store(static_cast<int>(GetPrivateProfileIntW(SEC, L"loop_speed_ms",   500, p)));
    param_jitter_ms      .store(static_cast<int>(GetPrivateProfileIntW(SEC, L"jitter_ms",       100, p)));
    param_press_base_ms  .store(static_cast<int>(GetPrivateProfileIntW(SEC, L"press_base_ms",    10, p)));
    param_press_jitter_ms.store(static_cast<int>(GetPrivateProfileIntW(SEC, L"press_jitter_ms",  10, p)));
    param_clicks_per_rest.store(static_cast<int>(GetPrivateProfileIntW(SEC, L"clicks_per_rest",   0, p)));
    param_rest_seconds   .store(static_cast<int>(GetPrivateProfileIntW(SEC, L"rest_seconds",       3, p)));
}

void save_config() {
    const std::wstring path = config_path();
    const wchar_t* p = path.c_str();

    auto write = [&](const wchar_t* key, int val) {
        wchar_t buf[32];
        _itow_s(val, buf, 10);
        WritePrivateProfileStringW(SEC, key, buf, p);
    };

    write(L"loop_speed_ms",   param_loop_speed_ms  .load());
    write(L"jitter_ms",       param_jitter_ms      .load());
    write(L"press_base_ms",   param_press_base_ms  .load());
    write(L"press_jitter_ms", param_press_jitter_ms.load());
    write(L"clicks_per_rest", param_clicks_per_rest.load());
    write(L"rest_seconds",    param_rest_seconds   .load());
}

} // namespace app
