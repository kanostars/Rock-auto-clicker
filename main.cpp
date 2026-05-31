#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "external/interception/interception.h"

// ========================================================================
// 共享状态
// ========================================================================
static std::atomic<bool>               is_macro_running{false};
static std::atomic<InterceptionDevice> active_mouse_id{0};
static InterceptionContext             g_context = nullptr;

// 用户可调参数 (毫秒 / 秒)
static std::atomic<int> param_loop_speed_ms{500};   // 循环基础速度
static std::atomic<int> param_jitter_ms{100};       // 每次循环的随机延迟范围
static std::atomic<int> param_clicks_per_rest{0};   // 0 = 不休息
static std::atomic<int> param_rest_seconds{3};      // 休息时长

// 统计
static std::atomic<unsigned long long> total_click_count{0};

// 日志
static std::mutex                g_log_mutex;
static std::vector<std::string>  g_log_lines;

static void add_log(const std::string& msg) {
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

// ========================================================================
// 连点器工作线程
// ========================================================================
[[noreturn]] static void macro_worker() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> offset_dist(-2, 2);
    std::uniform_int_distribution<int> press_dist(30, 50);

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

            std::this_thread::sleep_for(std::chrono::milliseconds(press_dist(gen)));

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
                // 休息时分段 sleep,以便快速响应停止指令
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

// ========================================================================
// Interception 事件循环线程 (拦截 Button 4 切换)
// ========================================================================
[[noreturn]] static void interception_thread() {
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

// ========================================================================
// DirectX11 + Win32 (ImGui 官方示例骨架)
// ========================================================================
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ImGui 内部的 Win32 消息处理
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ========================================================================
// GUI 渲染
// ========================================================================
static int g_active_tab = 0; // 0=首页 1=日志 2=测试

static void RenderGUI() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##main", nullptr, flags);
    ImGui::PopStyleVar(2);

    // ---- 顶部 LOGO 区 ----
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float header_h = 60.0f;
        ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + header_h);
        dl->AddRectFilled(p0, p1, IM_COL32(35, 90, 160, 255));

        // 简易 logo:一个圆+缩写
        float cx = p0.x + 30, cy = p0.y + header_h * 0.5f;
        dl->AddCircleFilled(ImVec2(cx, cy), 18.0f, IM_COL32(255, 255, 255, 255));
        dl->AddText(ImVec2(cx - 9, cy - 9), IM_COL32(35, 90, 160, 255), "BC");

        // 标题
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 60, p0.y + 18));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextUnformatted(u8"北辰连点器 v1.0");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + header_h + 6));
    }

    // ---- 底部按钮高度预留 ----
    const float bottom_h = 56.0f;
    float content_h = ImGui::GetContentRegionAvail().y - bottom_h;
    if (content_h < 100) content_h = 100;

    ImGui::BeginChild("##content", ImVec2(0, content_h), false);

    // ---- 标签页 ----
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem(u8"首页")) {
            g_active_tab = 0;
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextUnformatted(u8"点击参数设置");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 6));

            int loop_ms = param_loop_speed_ms.load();
            if (ImGui::InputInt(u8"循环速度 (毫秒)", &loop_ms, 50, 100)) {
                if (loop_ms < 1) loop_ms = 1;
                if (loop_ms > 60000) loop_ms = 60000;
                param_loop_speed_ms.store(loop_ms);
            }
            ImGui::TextDisabled(u8"  每次点击之间的基础间隔时间");
            ImGui::Dummy(ImVec2(0, 4));

            int jitter = param_jitter_ms.load();
            if (ImGui::InputInt(u8"随机延迟 (毫秒)", &jitter, 10, 50)) {
                if (jitter < 0) jitter = 0;
                if (jitter > 5000) jitter = 5000;
                param_jitter_ms.store(jitter);
            }
            ImGui::TextDisabled(u8"  每次循环额外加上 0~该值 的随机抖动");
            ImGui::Dummy(ImVec2(0, 8));

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextUnformatted(u8"休息策略");
            ImGui::Dummy(ImVec2(0, 4));

            int per_rest = param_clicks_per_rest.load();
            if (ImGui::InputInt(u8"点击多少次后休息", &per_rest, 1, 10)) {
                if (per_rest < 0) per_rest = 0;
                if (per_rest > 100000) per_rest = 100000;
                param_clicks_per_rest.store(per_rest);
            }
            ImGui::TextDisabled(u8"  设为 0 表示不休息,持续点击");

            int rest_s = param_rest_seconds.load();
            if (ImGui::InputInt(u8"休息时长 (秒)", &rest_s, 1, 5)) {
                if (rest_s < 1) rest_s = 1;
                if (rest_s > 3600) rest_s = 3600;
                param_rest_seconds.store(rest_s);
            }

            ImGui::Dummy(ImVec2(0, 10));
            ImGui::Separator();
            ImGui::Text(u8"累计点击: %llu 次", total_click_count.load());
            ImGui::Text(u8"活跃鼠标 ID: %d %s",
                        (int)active_mouse_id.load(),
                        active_mouse_id.load() == 0 ? u8"(请先动一下鼠标)" : "");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(u8"日志")) {
            g_active_tab = 1;
            ImGui::Dummy(ImVec2(0, 6));
            if (ImGui::Button(u8"清空日志")) {
                std::lock_guard<std::mutex> g(g_log_mutex);
                g_log_lines.clear();
            }
            ImGui::SameLine();
            ImGui::TextDisabled(u8"  (功能开发中,当前展示运行日志)");
            ImGui::Separator();

            ImGui::BeginChild("##log_scroll", ImVec2(0, 0), true);
            {
                std::lock_guard<std::mutex> g(g_log_mutex);
                for (const auto& line : g_log_lines) {
                    ImGui::TextUnformatted(line.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(u8"测试")) {
            g_active_tab = 2;
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextUnformatted(u8"按键测试 (开发中)");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextUnformatted(u8"当前触发键: 鼠标右后侧键 (Button 4 / XBUTTON1)");
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextDisabled(u8"  按下该键可切换连点器开关。");
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::TextDisabled(u8"自定义按键映射功能将在后续版本提供。");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // ---- 底部固定按钮 ----
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
    {
        bool running = is_macro_running.load();
        ImVec4 col = running ? ImVec4(0.85f, 0.30f, 0.30f, 1.0f)
                             : ImVec4(0.20f, 0.65f, 0.35f, 1.0f);
        ImVec4 col_h = running ? ImVec4(0.95f, 0.40f, 0.40f, 1.0f)
                               : ImVec4(0.30f, 0.75f, 0.45f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col_h);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);

        const char* label = running ? u8"暂停 (Button 4)" : u8"开始 (Button 4)";
        float btn_w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button(label, ImVec2(btn_w, 40))) {
            // 如果还没有活跃鼠标 ID,尝试默认第一个
            if (active_mouse_id.load() == 0) {
                active_mouse_id.store(INTERCEPTION_MOUSE(0));
                add_log("未检测到活跃鼠标,默认使用 MOUSE(0)");
            }
            bool now = !is_macro_running.load();
            is_macro_running.store(now);
            add_log(std::string("[GUI] 切换 -> ") + (now ? "开启" : "关闭"));
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::End();
}

// ========================================================================
// WinMain
// ========================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // ---- Interception 初始化 ----
    g_context = interception_create_context();
    if (!g_context) {
        MessageBoxW(nullptr,
                    L"无法创建 Interception 上下文。\n请确认:\n"
                    L"1. 已通过 install-interception.exe /install 安装内核驱动\n"
                    L"2. 系统已重启\n"
                    L"3. 程序以管理员身份运行",
                    L"北辰连点器 - 启动失败", MB_ICONERROR);
        return 1;
    }
    interception_set_filter(g_context, interception_is_mouse, INTERCEPTION_FILTER_MOUSE_ALL);

    // ---- 启动后台线程 ----
    std::thread(macro_worker).detach();
    std::thread(interception_thread).detach();
    add_log("北辰连点器 v1.0 已启动");

    // ---- 创建窗口 ----
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       hInstance, nullptr, nullptr, nullptr, nullptr,
                       L"BeiChenClicker", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"北辰连点器 v1.0",
                              WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                              100, 100, 460, 600,
                              nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // ---- ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // 中文字体
    ImFontConfig fc;
    fc.OversampleH = 2;
    fc.OversampleV = 2;
    const char* font_path = "C:\\Windows\\Fonts\\msyh.ttc";
    DWORD attr = GetFileAttributesA(font_path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        font_path = "C:\\Windows\\Fonts\\simhei.ttf"; // 备选
    }
    io.Fonts->AddFontFromFileTTF(font_path, 17.0f, &fc,
                                  io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // ---- 主循环 ----
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight,
                                        DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderGUI();

        ImGui::Render();
        const float clear_color[4] = { 0.96f, 0.96f, 0.97f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    interception_destroy_context(g_context);
    return 0;
}

// ========================================================================
// DX11 helpers
// ========================================================================
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice = nullptr; }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth  = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
