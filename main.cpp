#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <thread>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "core/app_state.h"
#include "core/clicker.h"
#include "platform/config.h"
#include "platform/d3d_window.h"
#include "ui/gui.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // ---- Interception 初始化 ----
    app::g_context = interception_create_context();
    if (!app::g_context) {
        MessageBoxW(nullptr,
                    L"无法创建 Interception 上下文。\n请确认:\n"
                    L"1. 已通过 install-interception.exe /install 安装内核驱动\n"
                    L"2. 系统已重启\n"
                    L"3. 程序以管理员身份运行",
                    L"连点器 - 启动失败", MB_ICONERROR);
        return 1;
    }
    interception_set_filter(app::g_context, interception_is_mouse, INTERCEPTION_FILTER_MOUSE_ALL);

    app::load_config();

    // ---- 启动后台线程 ----
    std::thread(app::macro_worker).detach();
    std::thread(app::interception_thread).detach();
    std::thread(app::hotkey_poller).detach();
    app::add_log("连点器 v1.1 已启动", app::LogLevel::INFO);

    // ---- 创建窗口 ----
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, app::wnd_proc, 0L, 0L,
                       hInstance, nullptr, nullptr, nullptr, nullptr,
                       L"BeiChenClicker", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"连点器 v1.1",
                              WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                              100, 100, 460, 640,
                              nullptr, nullptr, wc.hInstance, nullptr);

    if (!app::create_device_d3d(hwnd)) {
        app::cleanup_device_d3d();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    app::set_gui_hwnd(hwnd);

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
    const char* font_path = R"(C:\Windows\Fonts\msyh.ttc)";
    DWORD attr = GetFileAttributesA(font_path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        font_path = R"(C:\Windows\Fonts\simhei.ttf)"; // 备选
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
    ImGui_ImplDX11_Init(app::d3d_device(), app::d3d_context());

    // ---- 加载 logo 纹理(从 exe 同目录的 assets/ 找)----
    {
        wchar_t exe_dir[MAX_PATH];
        GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
        wchar_t* last_slash = wcsrchr(exe_dir, L'\\');
        if (last_slash) *(last_slash + 1) = L'\0';

        // wchar → char(仅 ASCII 路径)
        char path[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, exe_dir, -1, path, MAX_PATH, nullptr, nullptr);
        strncat_s(path, "assets\\favicon.png", MAX_PATH);

        int lw = 0, lh = 0;
        auto* srv = app::load_texture(path, &lw, &lh);
        app::set_logo_texture(srv, lw, lh);
        if (!srv) app::add_log("[警告] logo 图片加载失败，使用默认图标", app::LogLevel::WARN);
    }

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

        if (app::g_resize_width != 0 && app::g_resize_height != 0) {
            app::cleanup_render_target();
            app::d3d_swap_chain()->ResizeBuffers(0, app::g_resize_width, app::g_resize_height,
                                                  DXGI_FORMAT_UNKNOWN, 0);
            app::g_resize_width = app::g_resize_height = 0;
            app::create_render_target();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        app::render_gui();

        ImGui::Render();
        constexpr float clear_color[4] = { 0.96f, 0.96f, 0.97f, 1.0f };
        ID3D11RenderTargetView* rtv = app::d3d_main_rtv();
        app::d3d_context()->OMSetRenderTargets(1, &rtv, nullptr);
        app::d3d_context()->ClearRenderTargetView(rtv, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        app::d3d_swap_chain()->Present(1, 0);
    }

    app::save_config();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    app::cleanup_device_d3d();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    interception_destroy_context(app::g_context);
    return 0;
}
