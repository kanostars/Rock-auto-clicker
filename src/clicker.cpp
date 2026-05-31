#include "clicker.h"
#include "app_state.h"

#include <chrono>
#include <random>
#include <string>
#include <thread>

namespace app {

[[noreturn]] void macro_worker() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> offset_dist(-2, 2);
    // 每次点击重新读取参数,改参数后立即生效
    auto sample_press_ms = [&]() {
        const int base = param_press_base_ms.load();
        const int jitter_max = param_press_jitter_ms.load();
        if (jitter_max <= 0) return base;
        std::uniform_int_distribution<int> d(0, jitter_max);
        return base + d(gen);
    };

    int clicks_this_round = 0;
    bool was_running = false;

    while (true) {
        const bool running = is_macro_running.load();
        const InterceptionDevice mouse = active_mouse_id.load();

        if (running && mouse != 0) {
            if (!was_running) {
                add_log("连点循环已开启");
                was_running = true;
                clicks_this_round = 0;
            }

            // 1. 微小手抖
            InterceptionMouseStroke move_stroke = {};
            move_stroke.flags = INTERCEPTION_MOUSE_MOVE_RELATIVE;
            move_stroke.x = offset_dist(gen);
            move_stroke.y = offset_dist(gen);
            interception_send(g_context, mouse, (InterceptionStroke*)&move_stroke, 1);

            // 2. 左键按下
            InterceptionMouseStroke click_down = {};
            click_down.state = INTERCEPTION_MOUSE_LEFT_BUTTON_DOWN;
            interception_send(g_context, mouse, (InterceptionStroke*)&click_down, 1);

            std::this_thread::sleep_for(std::chrono::milliseconds(sample_press_ms()));

            // 3. 左键弹起
            InterceptionMouseStroke click_up = {};
            click_up.state = INTERCEPTION_MOUSE_LEFT_BUTTON_UP;
            interception_send(g_context, mouse, (InterceptionStroke*)&click_up, 1);

            total_click_count.fetch_add(1);
            clicks_this_round++;

            // 4. 是否需要休息
            const int per_rest = param_clicks_per_rest.load();
            const int rest_sec = param_rest_seconds.load();
            if (per_rest > 0 && clicks_this_round >= per_rest) {
                add_log("已完成 " + std::to_string(per_rest) + " 次点击,休息 " +
                        std::to_string(rest_sec) + " 秒");
                for (int i = 0; i < rest_sec * 10 && is_macro_running.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                clicks_this_round = 0;
            } else {
                // 5. 下一次延迟 = 基础速度 + 随机抖动
                const int base = param_loop_speed_ms.load();
                const int jitter_max = param_jitter_ms.load();
                int jitter = 0;
                if (jitter_max > 0) {
                    std::uniform_int_distribution<int> d(0, jitter_max);
                    jitter = d(gen);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(base + jitter));
            }
        } else {
            if (was_running) {
                add_log("连点循环已停止");
                was_running = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

[[noreturn]] void interception_thread() {
    InterceptionDevice device;
    InterceptionStroke stroke;

    while (interception_receive(g_context, device = interception_wait(g_context), &stroke, 1) > 0) {
        if (interception_is_mouse(device)) {
            auto& mouse_stroke = reinterpret_cast<InterceptionMouseStroke&>(stroke);

            // 任何鼠标动作都更新当前活跃鼠标,便于 GUI 按钮在没按过侧键时也能工作
            active_mouse_id.store(device);

            if (mouse_stroke.state & INTERCEPTION_MOUSE_BUTTON_4_DOWN) {
                bool now = !is_macro_running.load();
                is_macro_running.store(now);
                add_log(std::string("[侧键] 切换 -> ") + (now ? "开启" : "关闭"));
                continue; // 吞掉这次按下
            }
        }
        interception_send(g_context, device, &stroke, 1);
    }
    // 不会到达
}

} // namespace app
