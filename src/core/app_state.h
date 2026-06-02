#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "interception.h"

namespace app {

// 共享运行时状态
extern std::atomic<bool>               is_macro_running;
extern std::atomic<InterceptionDevice> active_mouse_id;
extern std::atomic<InterceptionDevice> active_keyboard_id; // 键盘设备，供驱动级按键注入使用
extern InterceptionContext             g_context;

// 用户可调参数
extern std::atomic<int> param_loop_speed_ms;
extern std::atomic<int> param_jitter_ms;
extern std::atomic<int> param_press_base_ms;
extern std::atomic<int> param_press_jitter_ms;
extern std::atomic<int> param_clicks_per_rest;
extern std::atomic<int> param_rest_seconds;

// 狂暴模式
extern std::atomic<bool>    g_berserk_enabled;
extern std::atomic<uint8_t> g_berserk_mount_vk;
extern std::atomic<int>     g_berserk_lclick_ms;
extern std::atomic<int>     g_berserk_gap1_ms;
extern std::atomic<int>     g_berserk_mount_ms;
extern std::atomic<int>     g_berserk_gap2_ms;

// 用户自定义的默认参数（"恢复默认" 读取此处，"设为默认" 写入此处）
struct ParamDefaults {
    // 普通点击
    int loop_speed_ms     = 200;
    int jitter_ms         = 20;
    int press_base_ms     = 200;
    int press_jitter_ms   = 10;
    int clicks_per_rest   = 0;
    int rest_seconds      = 3;
    // 狂暴
    int berserk_lclick_ms = 110;
    int berserk_gap1_ms   = 100;
    int berserk_mount_ms  = 30;
    int berserk_gap2_ms   = 30;
};
extern ParamDefaults g_param_defaults;
extern std::mutex    g_param_defaults_mutex;

// 统计
extern std::atomic<unsigned long long> total_click_count;
extern std::atomic<unsigned long long> total_run_ms;
extern std::atomic<long long>          run_start_ms_epoch;

// 热键配置（由 g_hotkey_mutex 保护）
struct HotkeyConfig {
    bool    mouse_btn3 = true;    // 中键（默认触发键）
    bool    mouse_btn4 = false;
    bool    mouse_btn5 = false;
    bool    key_ctrl   = false;
    bool    key_shift  = false;
    bool    key_alt    = false;
    uint8_t vkeys[4]   = {};     // 最多 4 个额外虚拟键码，0 = 不启用
};

extern HotkeyConfig             g_hotkey;
extern std::mutex               g_hotkey_mutex;
extern std::atomic<bool>        g_hotkey_recording; // 录制模式中不触发切换

// 日志
enum class LogLevel { INFO, BEHAVIOR, WARN, ERR };

struct LogEntry {
    std::string text;
    LogLevel    level;
};

extern std::mutex            g_log_mutex;
extern std::vector<LogEntry> g_log_lines;

void add_log(const std::string& msg, LogLevel level = LogLevel::INFO);

} // namespace app
