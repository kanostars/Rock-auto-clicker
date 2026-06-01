#include "clicker.h"
#include "app_state.h"

#include <chrono>
#include <random>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

    auto now_ms = []() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };

    while (true) {
        const bool running = is_macro_running.load();
        const InterceptionDevice mouse = active_mouse_id.load();

        if (running && mouse != 0) {
            if (!was_running) {
                add_log("连点循环已开启", LogLevel::INFO);
                was_running = true;
                clicks_this_round = 0;
                run_start_ms_epoch.store(now_ms());
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
                        std::to_string(rest_sec) + " 秒", LogLevel::INFO);
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
                // 把这一段运行时长合入累计值,然后清空 start
                const long long start = run_start_ms_epoch.load();
                if (start > 0) {
                    total_run_ms.fetch_add(static_cast<unsigned long long>(now_ms() - start));
                }
                run_start_ms_epoch.store(0);
                add_log("连点循环已停止", LogLevel::INFO);
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

            if (!g_hotkey_recording.load() &&
                (mouse_stroke.state & (INTERCEPTION_MOUSE_BUTTON_3_DOWN |
                                       INTERCEPTION_MOUSE_BUTTON_4_DOWN |
                                       INTERCEPTION_MOUSE_BUTTON_5_DOWN))) {
                // 快照热键配置
                HotkeyConfig hk;
                { std::lock_guard<std::mutex> lk(g_hotkey_mutex); hk = g_hotkey; }

                bool mouse_hit = false;
                if (hk.mouse_btn3 && (mouse_stroke.state & INTERCEPTION_MOUSE_BUTTON_3_DOWN)) mouse_hit = true;
                if (hk.mouse_btn4 && (mouse_stroke.state & INTERCEPTION_MOUSE_BUTTON_4_DOWN)) mouse_hit = true;
                if (hk.mouse_btn5 && (mouse_stroke.state & INTERCEPTION_MOUSE_BUTTON_5_DOWN)) mouse_hit = true;

                if (mouse_hit) {
                    bool kb_ok = true;
                    if (hk.key_ctrl  && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) kb_ok = false;
                    if (hk.key_shift && !(GetAsyncKeyState(VK_SHIFT)   & 0x8000)) kb_ok = false;
                    if (hk.key_alt   && !(GetAsyncKeyState(VK_MENU)    & 0x8000)) kb_ok = false;
                    for (uint8_t vk : hk.vkeys) {
                        if (vk != 0 && !(GetAsyncKeyState(vk) & 0x8000)) { kb_ok = false; break; }
                    }

                    if (kb_ok) {
                        bool now = !is_macro_running.load();
                        is_macro_running.store(now);
                        add_log(std::string("[热键] 切换 -> ") + (now ? "开启" : "关闭"), LogLevel::BEHAVIOR);
                        continue; // 吞掉此次按下
                    }
                }
            }
        }
        interception_send(g_context, device, &stroke, 1);
    }
}

[[noreturn]] void hotkey_poller() {
    // 轮询处理纯键盘（或无鼠标侧键）组合
    // 鼠标侧键由 interception_thread 负责，此线程仅在组合不含鼠标侧键时生效
    bool was_active = false;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        if (g_hotkey_recording.load()) { was_active = false; continue; }

        HotkeyConfig hk;
        { std::lock_guard<std::mutex> lk(g_hotkey_mutex); hk = g_hotkey; }

        // 若热键含鼠标侧键，由 interception_thread 处理，跳过
        if (hk.mouse_btn3 || hk.mouse_btn4 || hk.mouse_btn5) { was_active = false; continue; }

        // 纯键盘组合：必须至少有一个键
        bool has_kb = hk.key_ctrl || hk.key_shift || hk.key_alt;
        for (uint8_t vk : hk.vkeys) if (vk) has_kb = true;
        if (!has_kb) { was_active = false; continue; }

        bool all_down = true;
        if (hk.key_ctrl  && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) all_down = false;
        if (hk.key_shift && !(GetAsyncKeyState(VK_SHIFT)   & 0x8000)) all_down = false;
        if (hk.key_alt   && !(GetAsyncKeyState(VK_MENU)    & 0x8000)) all_down = false;
        for (uint8_t vk : hk.vkeys) {
            if (vk != 0 && !(GetAsyncKeyState(vk) & 0x8000)) { all_down = false; break; }
        }

        // 上升沿触发
        if (all_down && !was_active) {
            bool now = !is_macro_running.load();
            is_macro_running.store(now);
            add_log(std::string("[热键] 切换 -> ") + (now ? "开启" : "关闭"), LogLevel::BEHAVIOR);
        }
        was_active = all_down;
    }
}

} // namespace app
