#include "app_state.h"

#include <ctime>

namespace app {

std::atomic<bool>               is_macro_running{false};
std::atomic<InterceptionDevice> active_mouse_id{0};
InterceptionContext             g_context = nullptr;

std::atomic<int> param_loop_speed_ms{500};
std::atomic<int> param_jitter_ms{100};
std::atomic<int> param_press_base_ms{10};
std::atomic<int> param_press_jitter_ms{10};
std::atomic<int> param_clicks_per_rest{0};
std::atomic<int> param_rest_seconds{3};

std::atomic<unsigned long long> total_click_count{0};

std::mutex                g_log_mutex;
std::vector<std::string>  g_log_lines;

void add_log(const std::string& msg) {
    char ts[16];
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_s(&tm_buf, &now);
    std::strftime(ts, sizeof(ts), "[%H:%M:%S] ", &tm_buf);

    std::lock_guard<std::mutex> g(g_log_mutex);
    g_log_lines.emplace_back(std::string(ts) + msg);
    if (g_log_lines.size() > 1000) {
        g_log_lines.erase(g_log_lines.begin(), g_log_lines.begin() + 200);
    }
}

} // namespace app
