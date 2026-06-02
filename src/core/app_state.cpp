#include "app_state.h"

#include <chrono>
#include <ctime>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace app {

std::atomic<bool>               is_macro_running{false};
std::atomic<InterceptionDevice> active_mouse_id{0};
std::atomic<InterceptionDevice> active_keyboard_id{0};
InterceptionContext             g_context = nullptr;

std::atomic<int> param_loop_speed_ms{500};
std::atomic<int> param_jitter_ms{100};
std::atomic<int> param_press_base_ms{10};
std::atomic<int> param_press_jitter_ms{10};
std::atomic<int> param_clicks_per_rest{0};
std::atomic<int> param_rest_seconds{3};

std::atomic<bool>    g_berserk_enabled{false};
std::atomic<uint8_t> g_berserk_mount_vk{VK_LSHIFT};
std::atomic<int>     g_berserk_lclick_ms{110};
std::atomic<int>     g_berserk_gap1_ms{100};
std::atomic<int>     g_berserk_mount_ms{30};
std::atomic<int>     g_berserk_gap2_ms{30};

ParamDefaults g_param_defaults{};
std::mutex    g_param_defaults_mutex;

std::atomic<unsigned long long> total_click_count{0};
std::atomic<unsigned long long> total_run_ms{0};
std::atomic<long long>          run_start_ms_epoch{0};

HotkeyConfig             g_hotkey;
std::mutex               g_hotkey_mutex;
std::atomic<bool>        g_hotkey_recording{false};

std::mutex               g_log_mutex;
std::vector<LogEntry>    g_log_lines;

void add_log(const std::string& msg, LogLevel level) {
    // 时间戳精确到毫秒: [HH:MM:SS.mmm]
    auto now_tp = std::chrono::system_clock::now();
    std::time_t now_t = std::chrono::system_clock::to_time_t(now_tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now_tp.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    localtime_s(&tm_buf, &now_t);
    char ts[24];
    snprintf(ts, sizeof(ts), "[%02d:%02d:%02d.%03lld] ",
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
             static_cast<long long>(ms.count()));

    std::lock_guard<std::mutex> g(g_log_mutex);
    g_log_lines.push_back({std::string(ts) + msg, level});
    if (g_log_lines.size() > 1000) {
        g_log_lines.erase(g_log_lines.begin(), g_log_lines.begin() + 200);
    }
}

} // namespace app
