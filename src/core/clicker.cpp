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

    // 中断感知 sleep:在 is_macro_running 变 false 时尽快返回
    auto interruptible_sleep = [](int total_ms) {
        const int step = 10;
        for (int t = 0; t < total_ms && is_macro_running.load(); t += step) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(step, total_ms - t)));
        }
    };

    // 通过 SendInput 发送虚拟键码(Interception 只过滤鼠标,不影响键盘)
    // 优先使用 Interception 驱动级注入，无法注入时降级到 SendInput
    auto send_vkey = [](uint8_t vk, bool down) {
        const InterceptionDevice kbd = active_keyboard_id.load();
        if (kbd != 0) {
            // 驱动级键盘注入：用 scan code
            UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
            WORD scan_low = static_cast<WORD>(scan & 0xFF);
            bool e0 = (scan >> 8) == 0xE0;
            InterceptionKeyStroke ks{};
            ks.code  = scan_low;
            ks.state = static_cast<unsigned short>(
                (down ? INTERCEPTION_KEY_DOWN : INTERCEPTION_KEY_UP) |
                (e0   ? INTERCEPTION_KEY_E0   : 0));
            interception_send(g_context, kbd, (InterceptionStroke*)&ks, 1);
            return;
        }
        // 降级：SendInput
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
        WORD scan_low = static_cast<WORD>(scan & 0xFF);
        bool extended = (scan >> 8) == 0xE0;
        if (scan_low == 0) {
            in.ki.wVk     = vk;
            in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        } else {
            in.ki.wScan   = scan_low;
            in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
            if (extended) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        SendInput(1, &in, sizeof(in));
    };

    while (true) {
        const bool running = is_macro_running.load();
        const InterceptionDevice mouse = active_mouse_id.load();

        if (running && mouse != 0) {
            if (!was_running) {
                add_log(g_berserk_enabled.load()
                        ? u8"狂暴循环已开启" : u8"连点循环已开启", LogLevel::INFO);
                was_running = true;
                clicks_this_round = 0;
                run_start_ms_epoch.store(now_ms());
            }

            // ============ 狂暴模式 ============
            if (g_berserk_enabled.load()) {
                // 1. 鼠标左键按下 → 持续时长 → 松开
                InterceptionMouseStroke s{};
                s.state = INTERCEPTION_MOUSE_LEFT_BUTTON_DOWN;
                interception_send(g_context, mouse, (InterceptionStroke*)&s, 1);
                interruptible_sleep(g_berserk_lclick_ms.load());
                if (!is_macro_running.load()) {
                    s = {}; s.state = INTERCEPTION_MOUSE_LEFT_BUTTON_UP;
                    interception_send(g_context, mouse, (InterceptionStroke*)&s, 1);
                    continue;
                }
                s = {}; s.state = INTERCEPTION_MOUSE_LEFT_BUTTON_UP;
                interception_send(g_context, mouse, (InterceptionStroke*)&s, 1);

                // 2. 间隔1
                interruptible_sleep(g_berserk_gap1_ms.load());
                if (!is_macro_running.load()) continue;

                // 3. 坐骑技能键按下 → 持续时长 → 松开
                const uint8_t mvk = g_berserk_mount_vk.load();
                if (mvk != 0) {
                    send_vkey(mvk, true);
                    interruptible_sleep(g_berserk_mount_ms.load());
                    send_vkey(mvk, false);
                }

                total_click_count.fetch_add(1);
                clicks_this_round++;

                // 4. 休息策略仍生效;否则循环间隔固定 50ms
                const int per_rest = param_clicks_per_rest.load();
                const int rest_sec = param_rest_seconds.load();
                if (per_rest > 0 && clicks_this_round >= per_rest) {
                    add_log("已完成 " + std::to_string(per_rest) + " 次循环,休息 " +
                            std::to_string(rest_sec) + " 秒", LogLevel::INFO);
                    interruptible_sleep(rest_sec * 1000);
                    clicks_this_round = 0;
                } else {
                    interruptible_sleep(g_berserk_gap2_ms.load());
                }
                continue;
            }

            // ============ 普通连点 ============
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

    // 同时过滤键盘(只记录设备 ID,所有键盘事件原样转发)
    interception_set_filter(g_context, interception_is_keyboard,
                            INTERCEPTION_FILTER_KEY_DOWN | INTERCEPTION_FILTER_KEY_UP);

    while (interception_receive(g_context, device = interception_wait(g_context), &stroke, 1) > 0) {

        // ---- 键盘事件:记录设备 ID 后直接转发 ----
        if (interception_is_keyboard(device)) {
            if (active_keyboard_id.load() == 0)
                active_keyboard_id.store(device);
            interception_send(g_context, device, &stroke, 1);
            continue;
        }

        // ---- 鼠标事件 ----
        if (interception_is_mouse(device)) {
            auto& mouse_stroke = reinterpret_cast<InterceptionMouseStroke&>(stroke);
            active_mouse_id.store(device);

            if (!g_hotkey_recording.load() &&
                (mouse_stroke.state & (INTERCEPTION_MOUSE_BUTTON_3_DOWN |
                                       INTERCEPTION_MOUSE_BUTTON_4_DOWN |
                                       INTERCEPTION_MOUSE_BUTTON_5_DOWN))) {
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
