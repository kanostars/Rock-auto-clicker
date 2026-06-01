#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "interception.h"

namespace app {

// 共享运行时状态
extern std::atomic<bool>               is_macro_running;
extern std::atomic<InterceptionDevice> active_mouse_id;
extern InterceptionContext             g_context;

// 用户可调参数
extern std::atomic<int> param_loop_speed_ms;
extern std::atomic<int> param_jitter_ms;
extern std::atomic<int> param_press_base_ms;     // 左键按下基础持续时间
extern std::atomic<int> param_press_jitter_ms;   // 按下持续时间的随机抖动上限
extern std::atomic<int> param_clicks_per_rest;
extern std::atomic<int> param_rest_seconds;

// 统计
extern std::atomic<unsigned long long> total_click_count;
extern std::atomic<unsigned long long> total_run_ms;       // 已累计的运行毫秒(不含暂停)
extern std::atomic<long long>          run_start_ms_epoch; // 当前运行开始的时间戳(ms),0 表示未在运行

// 日志
enum class LogLevel { INFO, BEHAVIOR, WARN, ERR };

struct LogEntry {
    std::string text;   // 已含时间戳的完整字符串
    LogLevel    level;
};

extern std::mutex                g_log_mutex;
extern std::vector<LogEntry>     g_log_lines;

void add_log(const std::string& msg, LogLevel level = LogLevel::INFO);

} // namespace app
