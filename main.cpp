#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <windows.h>8
#include "include/interception.h"

// 全局控制变量
std::atomic<bool> is_macro_running(false);
std::atomic<InterceptionDevice> active_mouse_id(0);
InterceptionContext context;

// 连点器异步工作线程
[[noreturn]] void macro_worker() {
    // 初始化随机数生成器（C++11 标准，比 rand() 更玄学、更像人类）
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> delay_dist(0, 100);     // 0.1s 的随机波动范围 (0-100ms)
    std::uniform_int_distribution<int> offset_dist(-2, 2);     // 坐标轻量偏移量 (-2 到 2 像素)
    std::uniform_int_distribution<int> press_dist(30, 50);     // 模拟按键按住的时间 (30-50ms)

    while (true) {
        if (is_macro_running && active_mouse_id != 0) {
            InterceptionDevice mouse = active_mouse_id;

            // 1. 模拟人类微小的手抖（轻量坐标偏移）
            InterceptionMouseStroke move_stroke = {};
            move_stroke.flags = INTERCEPTION_MOUSE_MOVE_RELATIVE; // 相对移动
            move_stroke.x = offset_dist(gen);
            move_stroke.y = offset_dist(gen);
            interception_send(context, mouse, (InterceptionStroke*)&move_stroke, 1);

            // 2. 模拟鼠标左键按下
            InterceptionMouseStroke click_down = {};
            click_down.state = INTERCEPTION_MOUSE_LEFT_BUTTON_DOWN;
            interception_send(context, mouse, (InterceptionStroke*)&click_down, 1);

            // 模拟手指按在键上的物理时间
            std::this_thread::sleep_for(std::chrono::milliseconds(press_dist(gen)));

            // 3. 模拟鼠标左键弹起
            InterceptionMouseStroke click_up = {};
            click_up.state = INTERCEPTION_MOUSE_LEFT_BUTTON_UP;
            interception_send(context, mouse, (InterceptionStroke*)&click_up, 1);

            // 4. 计算下一次循环的延迟：0.5s (500ms) + 0.1s (0-100ms 随机值)
            int next_delay = 500 + delay_dist(gen);
            std::this_thread::sleep_for(std::chrono::milliseconds(next_delay));
        } else {
            // 宏未开启时，降低 CPU 占用
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "驱动级连点器 v1.0" << std::endl;
    context = interception_create_context();
    if (!context) {
        std::cerr << "无法创建 Interception 上下文，请检查驱动是否安装成功！" << std::endl;
        return 1;
    }

    // 设置过滤规则：只捕获鼠标的按键操作
    interception_set_filter(context, interception_is_mouse, INTERCEPTION_FILTER_MOUSE_ALL);

    std::cout << "驱动级连点器已启动！" << std::endl;
    std::cout << "请点击【鼠标右后侧键(Button 4)】开启/关闭连点循环..." << std::endl;

    // 启动异步连点线程
    std::thread worker(macro_worker);
    worker.detach();

    InterceptionDevice device;
    InterceptionStroke stroke;

    // 主线程循环：负责拦截和处理输入事件
    while (interception_receive(context, device = interception_wait(context), &stroke, 1) > 0) {
        if (interception_is_mouse(device)) {
            auto &mouse_stroke = reinterpret_cast<InterceptionMouseStroke &>(stroke);

            // 检测鼠标右后侧键按下 (XBUTTON1 / Button 4)
            if (mouse_stroke.state & INTERCEPTION_MOUSE_BUTTON_4_DOWN) {
                active_mouse_id = device;         // 记录当前活跃的鼠标 ID
                is_macro_running = !is_macro_running; // 切换宏状态

                std::cout << "宏状态切换 -> " << (is_macro_running ? "[开启 ON]" : "[关闭 OFF]") << std::endl;

                // 此时你可以选择不转发这个侧键的输入，达到“吞键”效果。
                // 如果你想让系统也响应这个侧键，就不要用 continue。
                continue;
            }
        }

        // 必须及时转发其余的所有正常鼠标操作，否则鼠标会卡死
        interception_send(context, device, &stroke, 1);
    }

    interception_destroy_context(context);
    return 0;
}