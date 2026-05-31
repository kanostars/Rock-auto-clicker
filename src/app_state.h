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
extern std::atomic<int> param_clicks_per_rest;
extern std::atomic<int> param_rest_seconds;

// 统计
extern std::atomic<unsigned long long> total_click_count;

// 日志缓冲(由 g_log_mutex 保护)
extern std::mutex                g_log_mutex;
extern std::vector<std::string>  g_log_lines;

void add_log(const std::string& msg);

} // namespace app
