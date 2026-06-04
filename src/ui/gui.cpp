#include "gui.h"
#include "core/app_state.h"
#include "platform/config.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "imgui.h"

namespace app {

static int g_active_tab = 0;

// logo 纹理
static ID3D11ShaderResourceView* g_logo_srv = nullptr;
static int g_logo_w = 0, g_logo_h = 0;

// 窗口句柄(用于置顶切换)
static HWND  g_hwnd       = nullptr;
static bool  g_always_top = false;

// 迷你模式(胶囊状态条)
static bool g_compact_mode = false;
static const int g_normal_window_w  = 460;
static const int g_normal_window_h  = 640;
static const int g_compact_window_w = 400;
static const int g_compact_window_h = 56;

// 胶囊拖动状态
static bool g_compact_dragging = false;
static ImVec2 g_drag_offset(0, 0);

static void toggle_pin() {
    g_always_top = !g_always_top;
    if (g_hwnd) {
        SetWindowPos(g_hwnd, g_always_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    add_log(g_always_top ? u8"窗口已置顶" : u8"窗口已取消置顶", LogLevel::INFO);
}

static void set_compact_mode(bool compact) {
    if (g_compact_mode == compact) return;
    g_compact_mode = compact;
    if (g_hwnd) {
        // 切换窗口样式:小窗模式下完全隐藏标题栏/边框,默认模式恢复为创建时的样式
        LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_POPUP | WS_BORDER);
        if (compact) {
            style |= WS_POPUP;
        } else {
            style |= (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME);
        }
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, style);

        RECT r; GetWindowRect(g_hwnd, &r);
        SetWindowPos(g_hwnd, nullptr,
                     r.left, r.top,
                     compact ? g_compact_window_w : g_normal_window_w,
                     compact ? g_compact_window_h : g_normal_window_h,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

        // 胶囊圆角:用 region 把窗口外框裁成胶囊形,四角变为真正的透明
        if (compact) {
            HRGN rgn = CreateRoundRectRgn(0, 0,
                g_compact_window_w + 1, g_compact_window_h + 1,
                g_compact_window_h, g_compact_window_h);
            SetWindowRgn(g_hwnd, rgn, TRUE);
        } else {
            SetWindowRgn(g_hwnd, nullptr, TRUE);
        }
    }
    add_log(compact ? u8"切换为迷你模式" : u8"已展开窗口", LogLevel::BEHAVIOR);
}

void set_logo_texture(ID3D11ShaderResourceView* srv, int w, int h) {
    g_logo_srv = srv;
    g_logo_w   = w;
    g_logo_h   = h;
}

void set_gui_hwnd(HWND hwnd) { g_hwnd = hwnd; }

// 把毫秒格式化成 HH:MM:SS
static std::string format_hms(unsigned long long ms) {
    unsigned long long s = ms / 1000;
    unsigned long long h = s / 3600;
    unsigned long long m = (s / 60) % 60;
    unsigned long long sec = s % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu", h, m, sec);
    return buf;
}

// 当前累计运行毫秒(含正在运行的本段)
static unsigned long long current_run_ms() {
    unsigned long long total = total_run_ms.load();
    long long start = run_start_ms_epoch.load();
    if (start > 0) {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        if (now > start) total += static_cast<unsigned long long>(now - start);
    }
    return total;
}

void render_gui() {
    const bool berserk = g_berserk_enabled.load();

    if (g_compact_mode) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGuiWindowFlags cflags = ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##compact", nullptr, cflags);
        ImGui::PopStyleVar(3);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();
        ImVec2 sz = ImGui::GetWindowSize();
        ImVec2 p1 = ImVec2(p0.x + sz.x, p0.y + sz.y);

        // 主题色
        ImU32 bg_top    = berserk ? IM_COL32(178, 45, 45, 255) : IM_COL32(48, 105, 178, 255);
        ImU32 bg_bottom = berserk ? IM_COL32(140, 28, 28, 255) : IM_COL32(28,  78, 145, 255);
        ImU32 bg        = berserk ? IM_COL32(160, 35, 35, 255) : IM_COL32(35,  90, 160, 255);

        // 胶囊背景:渐变 + 微高光
        float radius = sz.y * 0.5f;
        dl->AddRectFilledMultiColor(p0, p1, bg_top, bg_top, bg_bottom, bg_bottom);
        // 顶部高光带(覆盖到圆角内)
        dl->AddRectFilled(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y + sz.y * 0.45f),
                          IM_COL32(255, 255, 255, 22));
        // 圆角遮罩(用四角白色透明圆模拟,不必要;直接画外边框圆角)
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 40), radius, 0, 1.5f);

        // ---- LOGO + 置顶圆点 ----
        float cy = p0.y + sz.y * 0.5f;
        float cx = p0.x + radius;
        const float logo_size = sz.y - 22.0f; // 上下各留 11px

        ImGui::SetCursorScreenPos(ImVec2(cx - logo_size * 0.5f, cy - logo_size * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.18f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1,1,1,0.28f));
        if (ImGui::Button("##c_logo_btn", ImVec2(logo_size, logo_size))) {
            toggle_pin();
        }
        ImGui::PopStyleColor(3);

        if (g_logo_srv && g_logo_w > 0 && g_logo_h > 0) {
            float dw, dh;
            if (g_logo_w >= g_logo_h) {
                dw = logo_size; dh = logo_size * g_logo_h / g_logo_w;
            } else {
                dh = logo_size; dw = logo_size * g_logo_w / g_logo_h;
            }
            ImVec2 ip(cx - dw * 0.5f, cy - dh * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)g_logo_srv, ip,
                         ImVec2(ip.x + dw, ip.y + dh));
        } else {
            dl->AddCircleFilled(ImVec2(cx, cy), logo_size * 0.5f, IM_COL32(255,255,255,255));
            ImVec2 t_sz = ImGui::CalcTextSize("BC");
            dl->AddText(ImVec2(cx - t_sz.x * 0.5f, cy - t_sz.y * 0.5f), bg, "BC");
        }
        if (g_always_top) {
            float pin_off = logo_size * 0.42f;
            dl->AddCircleFilled(ImVec2(cx + pin_off, cy - pin_off), 4.5f,
                                IM_COL32(255, 220, 50, 255));
            dl->AddCircle(     ImVec2(cx + pin_off, cy - pin_off), 4.5f,
                                IM_COL32(180, 130, 0,  255), 0, 1.0f);
        }

        // ---- 三个等宽模块:连点器 / 点击 / 时长(每个模块文字居中,单行) ----
        float text_x = p0.x + radius * 2 + 4;
        float dot_r = 5.0f;
        float right_pad = radius * 0.5f + 14;       // 右上角圆点为锚
        float content_right = p1.x - right_pad;

        float section_total_w = content_right - text_x;
        float section_w       = section_total_w / 3.0f;
        float sec1_left = text_x;
        float sec2_left = sec1_left + section_w;
        float sec3_left = sec2_left + section_w;

        std::string sec2_text = std::string(u8"点击:") + std::to_string(total_click_count.load());
        std::string sec3_text = std::string(u8"时长:") + format_hms(current_run_ms());

        ImGui::SetWindowFontScale(1.0f); // 调整字体大小以适配更小的高度

        // 模块1:标题(整块可拖拽,长按可拖动窗口,点击展开主窗口)
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.15f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1,1,1,0.25f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        ImGui::SetCursorScreenPos(ImVec2(sec1_left, p0.y + 4));
        
        ImGui::Button(u8"返回##c_title", ImVec2(section_w, sz.y - 8));
        
        // 处理拖动逻辑
        if (ImGui::IsItemActive() && g_hwnd) {
            if (!g_compact_dragging && ImGui::GetMouseDragDelta(0).x == 0 && ImGui::GetMouseDragDelta(0).y == 0) {
                // 刚开始按下,记录偏移
                POINT cursor_pos;
                GetCursorPos(&cursor_pos);
                RECT r;
                GetWindowRect(g_hwnd, &r);
                g_drag_offset = ImVec2((float)(cursor_pos.x - r.left), (float)(cursor_pos.y - r.top));
            }
            
            if (ImGui::IsMouseDragging(0, 2.0f)) { // 拖动阈值 2px
                g_compact_dragging = true;
                POINT cursor_pos;
                GetCursorPos(&cursor_pos);
                SetWindowPos(g_hwnd, nullptr,
                    cursor_pos.x - (int)g_drag_offset.x,
                    cursor_pos.y - (int)g_drag_offset.y,
                    0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        
        // 松开鼠标时检测是否是点击(而非拖动)
        if (ImGui::IsItemDeactivated()) {
            if (!g_compact_dragging) {
                // 这是点击,展开窗口
                set_compact_mode(false);
            }
            g_compact_dragging = false;
        }
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        // 模块2、3:统计文本(直接绘制,水平+垂直居中)
        ImU32 val_col = IM_COL32(255, 255, 255, 245);
        ImVec2 sec2_sz = ImGui::CalcTextSize(sec2_text.c_str());
        ImVec2 sec3_sz = ImGui::CalcTextSize(sec3_text.c_str());
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(sec2_left + (section_w - sec2_sz.x) * 0.5f, cy - sec2_sz.y * 0.5f),
                    val_col, sec2_text.c_str());
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(sec3_left + (section_w - sec3_sz.x) * 0.5f, cy - sec3_sz.y * 0.5f),
                    val_col, sec3_text.c_str());

        ImGui::SetWindowFontScale(1.0f);

        // 模块之间的竖向分割线
        ImU32 sep_col = IM_COL32(255, 255, 255, 60);
        float sep_top    = p0.y + 10;
        float sep_bottom = p1.y - 10;
        dl->AddLine(ImVec2(sec2_left, sep_top), ImVec2(sec2_left, sep_bottom), sep_col, 1.0f);
        dl->AddLine(ImVec2(sec3_left, sep_top), ImVec2(sec3_left, sep_bottom), sep_col, 1.0f);

        // ---- 右上角运行状态圆点 + 光晕 ----
        bool running = is_macro_running.load();
        ImU32 dot_col   = running ? IM_COL32(80, 230, 100, 255) : IM_COL32(225, 80, 80, 255);
        ImU32 glow_col  = running ? IM_COL32(80, 230, 100, 70)  : IM_COL32(225, 80, 80, 70);
        // 紧挨时长文字右边,垂直居中
        float sec3_text_right = sec3_left + (section_w + sec3_sz.x) * 0.5f;
        ImVec2 dot_pos(sec3_text_right + 10, cy);
        dl->AddCircleFilled(dot_pos, dot_r * 1.9f, glow_col);
        dl->AddCircleFilled(dot_pos, dot_r,        dot_col);
        dl->AddCircle(      dot_pos, dot_r,        IM_COL32(255, 255, 255, 220), 0, 1.2f);

        ImGui::End();
        return;
    }

    // 狂暴模式:整体偏红(Tab/标题/标签等控件)
    int color_pushes = 0;
    if (berserk) {
        ImGui::PushStyleColor(ImGuiCol_TabActive,           ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TabHovered,          ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Tab,                 ImVec4(0.65f, 0.20f, 0.20f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive,  ImVec4(0.75f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,             ImVec4(0.98f, 0.92f, 0.92f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,      ImVec4(1.00f, 0.86f, 0.86f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,       ImVec4(1.00f, 0.80f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Separator,           ImVec4(0.85f, 0.25f, 0.25f, 0.6f));
        color_pushes = 8;
    }

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
        float region_w = ImGui::GetContentRegionAvail().x;
        ImVec2 p1 = ImVec2(p0.x + region_w, p0.y + header_h);
        dl->AddRectFilled(p0, p1, berserk ? IM_COL32(160, 35, 35, 255) : IM_COL32(35, 90, 160, 255));

        float cx = p0.x + 30, cy = p0.y + header_h * 0.5f;
        const float logo_size = 36.0f;

        // 透明可点击区域覆盖 logo 位置，检测点击切换置顶
        ImGui::SetCursorScreenPos(ImVec2(cx - logo_size * 0.5f, cy - logo_size * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.15f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1,1,1,0.25f));
        if (ImGui::Button("##logo_btn", ImVec2(logo_size, logo_size))) {
            toggle_pin();
        }
        ImGui::PopStyleColor(3);

        // 实际图像/文字（画在按钮下方，不阻断点击）
        if (g_logo_srv && g_logo_w > 0 && g_logo_h > 0) {
            float draw_w, draw_h;
            if (g_logo_w >= g_logo_h) {
                draw_w = logo_size;
                draw_h = logo_size * g_logo_h / g_logo_w;
            } else {
                draw_h = logo_size;
                draw_w = logo_size * g_logo_w / g_logo_h;
            }
            ImVec2 img_pos(cx - draw_w * 0.5f, cy - draw_h * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)g_logo_srv,
                         img_pos, ImVec2(img_pos.x + draw_w, img_pos.y + draw_h));
        } else {
            dl->AddCircleFilled(ImVec2(cx, cy), 18.0f, IM_COL32(255, 255, 255, 255));
            dl->AddText(ImVec2(cx - 9, cy - 9),
                        berserk ? IM_COL32(160, 35, 35, 255) : IM_COL32(35, 90, 160, 255), "BC");
        }

        // 置顶时在 logo 右上角画小图钉指示
        if (g_always_top) {
            dl->AddCircleFilled(ImVec2(cx + 14, cy - 14), 5.0f, IM_COL32(255, 220, 50, 255));
            dl->AddCircle(     ImVec2(cx + 14, cy - 14), 5.0f, IM_COL32(200, 160, 0,  255));
        }

        // 标题文本(可点击切换迷你模式,垂直居中于 header)
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.15f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1,1,1,0.25f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        ImGui::SetWindowFontScale(1.4f);
        ImVec2 title_sz = ImGui::CalcTextSize(u8"连点器 v1.1");
        float title_btn_w = title_sz.x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 60, p0.y));
        if (ImGui::Button(u8"连点器 v1.1##title_btn", ImVec2(title_btn_w, header_h))) {
            set_compact_mode(true);
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        // 右上角:本次运行时长(不含暂停)
        {
            std::string elapsed = format_hms(current_run_ms());
            std::string label = std::string(u8"运行 ") + elapsed;
            ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            float pad = 12.0f;
            dl->AddText(ImVec2(p1.x - ts.x - pad, p0.y + (header_h - ts.y) * 0.5f),
                        IM_COL32(255, 255, 255, 235), label.c_str());
        }

        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + header_h + 6));
    }

    // ---- 统计条(logo 与标签页之间, 醒目展示累计点击 + 活跃鼠标 ID) ----
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float region_w = ImGui::GetContentRegionAvail().x;
        float bar_h = 56.0f;
        ImVec2 p1 = ImVec2(p0.x + region_w, p0.y + bar_h);
        dl->AddRectFilled(p0, p1, IM_COL32(245, 248, 252, 255));
        dl->AddRect(p0, p1, IM_COL32(210, 220, 232, 255));
        float mid_x = p0.x + region_w * 0.5f;
        dl->AddLine(ImVec2(mid_x, p0.y + 8), ImVec2(mid_x, p1.y - 8),
                    IM_COL32(210, 220, 232, 255));

        const InterceptionDevice mid = active_mouse_id.load();

        // 左格:累计点击
        {
            std::string num = std::to_string(total_click_count.load());
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 12, p0.y + 6));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(110, 120, 135, 255));
            ImGui::TextUnformatted(u8"累计点击");
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 12, p0.y + 24));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(35, 90, 160, 255));
            ImGui::SetWindowFontScale(1.5f);
            ImGui::TextUnformatted(num.c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
        }

        // 右格:活跃鼠标 ID
        {
            ImGui::SetCursorScreenPos(ImVec2(mid_x + 12, p0.y + 6));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(110, 120, 135, 255));
            ImGui::TextUnformatted(u8"活跃鼠标");
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(mid_x + 12, p0.y + 24));
            if (mid == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 80, 80, 255));
                ImGui::SetWindowFontScale(1.1f);
                ImGui::TextUnformatted(u8"请先动一下鼠标");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(35, 90, 160, 255));
                ImGui::SetWindowFontScale(1.5f);
                ImGui::Text("ID %d", (int)mid);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 6));
    }

    const float bottom_h = 56.0f;
    float content_h = ImGui::GetContentRegionAvail().y - bottom_h;
    if (content_h < 100) content_h = 100;

    ImGui::BeginChild("##content", ImVec2(0, content_h), false);

    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem(u8"首页")) {
            g_active_tab = 0;
            ImGui::Dummy(ImVec2(0, 8));

            // 狂暴模式下隐藏点击参数和按键时长,只保留休息策略
            if (!berserk) {
            // "点击参数设置" 标题 + 右侧预设按钮同行
            ImGui::TextUnformatted(u8"点击参数设置");
            ImGui::SameLine();
            {
                // 三个小按钮右对齐: 设为默认 / 默认 / 点射
                const float btn_w = 48.0f;
                const float save_w = 76.0f; // "设为默认" 较宽
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x
                                     - save_w - btn_w * 2 - spacing * 2);

                auto apply_preset = [](int loop, int jitter, int press_base, int press_jitter,
                                       const char* name) {
                    param_loop_speed_ms  .store(loop);
                    param_jitter_ms      .store(jitter);
                    param_press_base_ms  .store(press_base);
                    param_press_jitter_ms.store(press_jitter);
                    add_log(std::string(u8"[预设] 切换至 ") + name, LogLevel::BEHAVIOR);
                };

                // 把当前点击参数写入用户默认
                if (ImGui::SmallButton(u8"设为默认##home")) {
                    {
                        std::lock_guard<std::mutex> lk(g_param_defaults_mutex);
                        g_param_defaults.loop_speed_ms   = param_loop_speed_ms  .load();
                        g_param_defaults.jitter_ms       = param_jitter_ms      .load();
                        g_param_defaults.press_base_ms   = param_press_base_ms  .load();
                        g_param_defaults.press_jitter_ms = param_press_jitter_ms.load();
                    }
                    save_config();
                    add_log(u8"[预设] 当前点击参数已保存为默认", LogLevel::BEHAVIOR);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"默认")) {
                    ParamDefaults d;
                    { std::lock_guard<std::mutex> lk(g_param_defaults_mutex); d = g_param_defaults; }
                    apply_preset(d.loop_speed_ms, d.jitter_ms, d.press_base_ms, d.press_jitter_ms,
                                 u8"默认");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"点射"))
                    apply_preset(350, 20, 30, 10, u8"点射");
            }

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 6));

            int loop_ms = param_loop_speed_ms.load();
            if (ImGui::InputInt(u8"循环速度 (毫秒)", &loop_ms, 50, 100)) {
                if (loop_ms < 1) loop_ms = 1;
                if (loop_ms > 60000) loop_ms = 60000;
                param_loop_speed_ms.store(loop_ms);
                add_log(u8"[参数] 循环速度 → " + std::to_string(loop_ms) + " ms", LogLevel::BEHAVIOR);
            }
            ImGui::TextDisabled(u8"  每次点击之间的基础间隔时间");
            ImGui::Dummy(ImVec2(0, 4));

            int jitter = param_jitter_ms.load();
            if (ImGui::InputInt(u8"随机延迟 (毫秒)", &jitter, 10, 50)) {
                if (jitter < 0) jitter = 0;
                if (jitter > 5000) jitter = 5000;
                param_jitter_ms.store(jitter);
                add_log(u8"[参数] 随机延迟 → " + std::to_string(jitter) + " ms", LogLevel::BEHAVIOR);
            }
            ImGui::TextDisabled(u8"  每次循环额外加上 0~该值 的随机抖动");
            ImGui::Dummy(ImVec2(0, 8));

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextUnformatted(u8"按键时长");
            ImGui::Dummy(ImVec2(0, 4));

            int press_base = param_press_base_ms.load();
            if (ImGui::InputInt(u8"按下时长 (毫秒)", &press_base, 1, 5)) {
                if (press_base < 1)     press_base = 1;
                if (press_base > 60000) press_base = 60000;
                param_press_base_ms.store(press_base);
                add_log(u8"[参数] 按下时长 → " + std::to_string(press_base) + " ms", LogLevel::BEHAVIOR);
            }
            ImGui::TextDisabled(u8"  左键按下后持续时间的基础值");

            int press_jitter = param_press_jitter_ms.load();
            if (ImGui::InputInt(u8"按下随机延迟 (毫秒)", &press_jitter, 1, 5)) {
                if (press_jitter < 0)     press_jitter = 0;
                if (press_jitter > 60000) press_jitter = 60000;
                param_press_jitter_ms.store(press_jitter);
                add_log(u8"[参数] 按下随机延迟 → " + std::to_string(press_jitter) + " ms", LogLevel::BEHAVIOR);
            }
            ImGui::TextDisabled(u8"  在基础值上额外加 0~该值 的随机抖动");
            ImGui::Dummy(ImVec2(0, 8));

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));
            } else {
                // 狂暴模式提示
                ImGui::TextColored(ImVec4(0.85f, 0.25f, 0.25f, 1.0f), u8"⚡ 狂暴模式已开启");
                ImGui::TextDisabled(u8"  循环固定: 左键%dms → 间隔%dms → 骑乘键%dms → 间隔%dms",
                    g_berserk_lclick_ms.load(), g_berserk_gap1_ms.load(),
                    g_berserk_mount_ms.load(),  g_berserk_gap2_ms.load());
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, 4));
            }
            ImGui::TextUnformatted(u8"休息策略");
            ImGui::Dummy(ImVec2(0, 4));

            int per_rest = param_clicks_per_rest.load();
            if (ImGui::InputInt(u8"点击多少次后休息", &per_rest, 1, 10)) {
                if (per_rest < 0) per_rest = 0;
                if (per_rest > 100000) per_rest = 100000;
                param_clicks_per_rest.store(per_rest);
                add_log(u8"[参数] 每轮点击数 → " + std::to_string(per_rest), LogLevel::BEHAVIOR);
            }
            ImGui::TextDisabled(u8"  设为 0 表示不休息,持续点击");

            int rest_s = param_rest_seconds.load();
            if (ImGui::InputInt(u8"休息时长 (秒)", &rest_s, 1, 5)) {
                if (rest_s < 1) rest_s = 1;
                if (rest_s > 3600) rest_s = 3600;
                param_rest_seconds.store(rest_s);
                add_log(u8"[参数] 休息时长 → " + std::to_string(rest_s) + " s", LogLevel::BEHAVIOR);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(u8"日志")) {
            g_active_tab = 1;

            // 过滤 + 清空，单行紧凑布局，无额外 padding
            static bool show_info     = true;
            static bool show_behavior = true;
            static bool show_warn     = true;
            static bool show_err      = true;
            ImGui::Checkbox("INFO",   &show_info);     ImGui::SameLine();
            ImGui::Checkbox(u8"行为", &show_behavior); ImGui::SameLine();
            ImGui::Checkbox("WARN",   &show_warn);     ImGui::SameLine();
            ImGui::Checkbox("ERROR",  &show_err);      ImGui::SameLine();
            // 把清空按钮推到最右侧
            float clear_w = ImGui::CalcTextSize(u8"清空").x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - clear_w);
            if (ImGui::Button(u8"清空")) {
                std::lock_guard<std::mutex> g(g_log_mutex);
                g_log_lines.clear();
            }
            ImGui::Separator();

            ImGui::BeginChild("##log_scroll", ImVec2(0, 0), true);
            {
                std::lock_guard<std::mutex> g(g_log_mutex);
                for (const auto& entry : g_log_lines) {
                    switch (entry.level) {
                        case LogLevel::INFO:
                            if (!show_info) continue;
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
                            break;
                        case LogLevel::BEHAVIOR:
                            if (!show_behavior) continue;
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.50f, 0.90f, 1.0f));
                            break;
                        case LogLevel::WARN:
                            if (!show_warn) continue;
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.60f, 0.10f, 1.0f));
                            break;
                        case LogLevel::ERR:
                            if (!show_err) continue;
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
                            break;
                    }
                    ImGui::TextUnformatted(entry.text.c_str());
                    ImGui::PopStyleColor();
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(u8"设置")) {
            g_active_tab = 2;
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextUnformatted(u8"触发按键配置");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));

            // 键码 → 可读名称
            auto vkey_name = [](uint8_t vk, char* buf, int bufsz) {
                if (vk >= VK_F1  && vk <= VK_F24) { snprintf(buf, bufsz, "F%d", vk - VK_F1 + 1); return; }
                if (vk >= '0'    && vk <= '9')    { buf[0] = (char)vk; buf[1] = 0; return; }
                if (vk >= 'A'    && vk <= 'Z')    { buf[0] = (char)vk; buf[1] = 0; return; }
                if (vk == VK_TAB)   { snprintf(buf, bufsz, "Tab");   return; }
                if (vk == VK_SPACE) { snprintf(buf, bufsz, "Space"); return; }
                snprintf(buf, bufsz, "VK 0x%02X", vk);
            };

            // 当前热键快照
            HotkeyConfig hk;
            { std::lock_guard<std::mutex> lk(g_hotkey_mutex); hk = g_hotkey; }

            // 组合描述字符串（用 parts 列表拼接，避免尾部 " + "）
            auto combo_str = [&]() -> std::string {
                std::vector<std::string> parts;
                if (hk.key_ctrl)  parts.push_back("Ctrl");
                if (hk.key_shift) parts.push_back("Shift");
                if (hk.key_alt)   parts.push_back("Alt");
                for (uint8_t vk : hk.vkeys) {
                    if (vk == 0) break;
                    char nb[16]; vkey_name(vk, nb, sizeof(nb));
                    parts.push_back(nb);
                }
                if (hk.mouse_btn3) parts.push_back(u8"中键");
                if (hk.mouse_btn4) parts.push_back("Button4");
                if (hk.mouse_btn5) parts.push_back("Button5");
                if (parts.empty()) return u8"（未设置）";
                std::string s;
                for (size_t i = 0; i < parts.size(); ++i) {
                    if (i > 0) s += " + ";
                    s += parts[i];
                }
                return s;
            };

            // 录制状态机
            enum class RecState { IDLE, WAITING, HOLDING, DONE };
            static RecState rec_state = RecState::IDLE;
            static HotkeyConfig rec_buf{};  // 录制过程中积累的组合

            // 扫描工具：返回当前按住的 HotkeyConfig（收集所有按下的键）
            auto scan_keys = []() -> HotkeyConfig {
                HotkeyConfig h{};
                h.mouse_btn3 = (GetAsyncKeyState(VK_MBUTTON)  & 0x8000) != 0;
                h.mouse_btn4 = (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
                h.mouse_btn5 = (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
                h.key_ctrl   = (GetAsyncKeyState(VK_CONTROL)  & 0x8000) != 0;
                h.key_shift  = (GetAsyncKeyState(VK_SHIFT)    & 0x8000) != 0;
                h.key_alt    = (GetAsyncKeyState(VK_MENU)     & 0x8000) != 0;
                // 收集所有按下的非修饰键（最多 4 个）
                static const int candidates[] = {
                    VK_TAB, VK_SPACE,
                    VK_F1,VK_F2,VK_F3,VK_F4,VK_F5,VK_F6,
                    VK_F7,VK_F8,VK_F9,VK_F10,VK_F11,VK_F12,
                    VK_F13,VK_F14,VK_F15,VK_F16,VK_F17,VK_F18,
                    VK_F19,VK_F20,VK_F21,VK_F22,VK_F23,VK_F24,
                };
                int idx = 0;
                for (int vk : candidates) {
                    if (idx >= 4) break;
                    if (GetAsyncKeyState(vk) & 0x8000) h.vkeys[idx++] = static_cast<uint8_t>(vk);
                }
                for (int vk = '0'; vk <= 'Z' && idx < 4; ++vk) {
                    if (GetAsyncKeyState(vk) & 0x8000) h.vkeys[idx++] = static_cast<uint8_t>(vk);
                }
                return h;
            };

            auto has_any = [](const HotkeyConfig& h) {
                bool has_vkey = false;
                for (uint8_t vk : h.vkeys) if (vk) { has_vkey = true; break; }
                return h.mouse_btn3 || h.mouse_btn4 || h.mouse_btn5 ||
                       h.key_ctrl  || h.key_shift   || h.key_alt    || has_vkey;
            };

            switch (rec_state) {
            case RecState::IDLE:
                ImGui::Text(u8"当前触发键: %s", combo_str().c_str());
                ImGui::Dummy(ImVec2(0, 8));
                if (ImGui::Button(u8"  录制新组合  ")) {
                    rec_buf = {};
                    g_hotkey_recording.store(true);
                    rec_state = RecState::WAITING;
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"恢复默认")) {
                    HotkeyConfig def{};  // 默认：仅中键
                    { std::lock_guard<std::mutex> lk(g_hotkey_mutex); g_hotkey = def; }
                    add_log(u8"[设置] 触发键已恢复默认（中键）", LogLevel::BEHAVIOR);
                }
                break;

            case RecState::WAITING:
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.1f, 1.0f),
                                   u8"请按住目标组合键…");
                ImGui::Dummy(ImVec2(0, 4));
                if (ImGui::SmallButton(u8"取消")) {
                    g_hotkey_recording.store(false);
                    rec_state = RecState::IDLE;
                }
                {
                    HotkeyConfig cur = scan_keys();
                    if (has_any(cur)) {
                        rec_buf = cur;
                        rec_state = RecState::HOLDING;
                    }
                }
                break;

            case RecState::HOLDING: {
                HotkeyConfig cur = scan_keys();
                if (has_any(cur)) {
                    if (cur.mouse_btn3) rec_buf.mouse_btn3 = true;
                    if (cur.mouse_btn4) rec_buf.mouse_btn4 = true;
                    if (cur.mouse_btn5) rec_buf.mouse_btn5 = true;
                    if (cur.key_ctrl)   rec_buf.key_ctrl   = true;
                    if (cur.key_shift)  rec_buf.key_shift  = true;
                    if (cur.key_alt)    rec_buf.key_alt    = true;
                    // 合并新出现的 vkeys
                    for (uint8_t vk : cur.vkeys) {
                        if (vk == 0) break;
                        bool found = false;
                        for (uint8_t rv : rec_buf.vkeys) if (rv == vk) { found = true; break; }
                        if (!found) {
                            for (uint8_t& rv : rec_buf.vkeys) { if (rv == 0) { rv = vk; break; } }
                        }
                    }
                }

                // 预览（同样用 parts 列表）
                std::vector<std::string> parts;
                if (rec_buf.key_ctrl)  parts.push_back("Ctrl");
                if (rec_buf.key_shift) parts.push_back("Shift");
                if (rec_buf.key_alt)   parts.push_back("Alt");
                for (uint8_t vk : rec_buf.vkeys) {
                    if (vk == 0) break;
                    char nb[16]; vkey_name(vk, nb, sizeof(nb));
                    parts.push_back(nb);
                }
                if (rec_buf.mouse_btn3) parts.push_back(u8"中键");
                if (rec_buf.mouse_btn4) parts.push_back("Button4");
                if (rec_buf.mouse_btn5) parts.push_back("Button5");
                std::string preview;
                for (size_t i = 0; i < parts.size(); ++i) {
                    if (i > 0) preview += " + ";
                    preview += parts[i];
                }

                ImGui::TextColored(ImVec4(0.2f, 0.7f, 0.3f, 1.0f),
                                   u8"检测到: %s", preview.c_str());
                ImGui::TextDisabled(u8"  松开所有按键即确认");
                ImGui::Dummy(ImVec2(0, 4));
                if (ImGui::SmallButton(u8"取消")) {
                    g_hotkey_recording.store(false);
                    rec_state = RecState::IDLE;
                }

                if (!has_any(cur)) {
                    { std::lock_guard<std::mutex> lk(g_hotkey_mutex); g_hotkey = rec_buf; }
                    add_log(std::string(u8"[设置] 触发键已更新 → ") + preview, LogLevel::BEHAVIOR);
                    g_hotkey_recording.store(false);
                    rec_state = RecState::IDLE;
                }
                break;
            }
            default: rec_state = RecState::IDLE; break;
            }

            // ---- 狂暴模式 ----
            ImGui::Dummy(ImVec2(0, 14));
            ImGui::TextUnformatted(u8"狂暴模式");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 6));

            bool berserk = g_berserk_enabled.load();
            ImGui::PushStyleColor(ImGuiCol_Button,
                berserk ? ImVec4(0.85f, 0.25f, 0.25f, 1.0f) : ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                berserk ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f) : ImVec4(0.65f, 0.65f, 0.68f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                berserk ? ImVec4(0.85f, 0.25f, 0.25f, 1.0f) : ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button(berserk ? u8"狂暴模式: 已开启" : u8"狂暴模式: 关闭", ImVec2(160, 0))) {
                g_berserk_enabled.store(!berserk);
                add_log(std::string(u8"[狂暴] ") + (!berserk ? u8"已开启" : u8"已关闭"),
                        LogLevel::BEHAVIOR);
            }
            ImGui::PopStyleColor(4);

            ImGui::SameLine();

            // 骑乘技能触发键(单键录制,区分左右修饰键)
            uint8_t cur_mvk = g_berserk_mount_vk.load();
            char mvk_label[24];
            switch (cur_mvk) {
                case VK_LSHIFT:   snprintf(mvk_label, sizeof(mvk_label), "Left Shift");  break;
                case VK_RSHIFT:   snprintf(mvk_label, sizeof(mvk_label), "Right Shift"); break;
                case VK_LCONTROL: snprintf(mvk_label, sizeof(mvk_label), "Left Ctrl");   break;
                case VK_RCONTROL: snprintf(mvk_label, sizeof(mvk_label), "Right Ctrl");  break;
                case VK_LMENU:    snprintf(mvk_label, sizeof(mvk_label), "Left Alt");    break;
                case VK_RMENU:    snprintf(mvk_label, sizeof(mvk_label), "Right Alt");   break;
                case VK_TAB:      snprintf(mvk_label, sizeof(mvk_label), "Tab");         break;
                case VK_SPACE:    snprintf(mvk_label, sizeof(mvk_label), "Space");       break;
                default:          vkey_name(cur_mvk, mvk_label, sizeof(mvk_label));      break;
            }

            static bool mvk_recording = false;
            if (!mvk_recording) {
                ImGui::Text(u8"骑乘键: %s", mvk_label);
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"录制##mvk")) {
                    mvk_recording = true;
                    g_hotkey_recording.store(true);
                }
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.1f, 1.0f), u8"按下骑乘技能键…");
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"取消##mvk")) {
                    mvk_recording = false;
                    g_hotkey_recording.store(false);
                }
                // 优先匹配左/右修饰键,再匹配普通键
                uint8_t captured = 0;
                static const int candidates[] = {
                    VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU,
                    VK_TAB, VK_SPACE,
                    VK_F1,VK_F2,VK_F3,VK_F4,VK_F5,VK_F6,VK_F7,VK_F8,
                    VK_F9,VK_F10,VK_F11,VK_F12,
                };
                for (int vk : candidates) {
                    if (GetAsyncKeyState(vk) & 0x8000) { captured = (uint8_t)vk; break; }
                }
                if (captured == 0) {
                    for (int vk = '0'; vk <= 'Z'; ++vk) {
                        if (GetAsyncKeyState(vk) & 0x8000) { captured = (uint8_t)vk; break; }
                    }
                }
                if (captured != 0) {
                    g_berserk_mount_vk.store(captured);
                    add_log(u8"[狂暴] 骑乘键已更新", LogLevel::BEHAVIOR);
                    mvk_recording = false;
                    g_hotkey_recording.store(false);
                }
            }

            ImGui::TextDisabled(u8"  循环: 左键按下 → 间隔1 → 骑乘键按下 → 间隔2(循环)");
            ImGui::TextDisabled(u8"  当前: 左键%dms → 间隔%dms → 骑乘键%dms → 间隔%dms",
                g_berserk_lclick_ms.load(), g_berserk_gap1_ms.load(),
                g_berserk_mount_ms.load(),  g_berserk_gap2_ms.load());
            ImGui::Dummy(ImVec2(0, 8));

            // 四个时长参数
            auto berserk_input = [](const char* label, std::atomic<int>& param,
                                    int lo, int hi, const char* hint) {
                int v = param.load();
                if (ImGui::InputInt(label, &v, 10, 50)) {
                    if (v < lo) v = lo;
                    if (v > hi) v = hi;
                    param.store(v);
                }
                ImGui::TextDisabled(hint);
            };

            berserk_input(u8"左键按下时长 (ms)", g_berserk_lclick_ms, 1, 5000,
                          u8"  左键按下并持续的时间");
            berserk_input(u8"间隔1 (ms)",        g_berserk_gap1_ms,   0, 5000,
                          u8"  左键松开到骑乘键按下的间隔");
            berserk_input(u8"骑乘键时长 (ms)",   g_berserk_mount_ms,  1, 5000,
                          u8"  骑乘技能键按下并持续的时间");
            berserk_input(u8"循环间隔 (ms)",     g_berserk_gap2_ms,   0, 5000,
                          u8"  骑乘键松开到下一次循环的间隔");

            ImGui::Dummy(ImVec2(0, 4));
            if (ImGui::SmallButton(u8"恢复默认##berserk")) {
                ParamDefaults d;
                { std::lock_guard<std::mutex> lk(g_param_defaults_mutex); d = g_param_defaults; }
                g_berserk_lclick_ms.store(d.berserk_lclick_ms);
                g_berserk_gap1_ms  .store(d.berserk_gap1_ms);
                g_berserk_mount_ms .store(d.berserk_mount_ms);
                g_berserk_gap2_ms  .store(d.berserk_gap2_ms);
                add_log(u8"[狂暴] 参数已恢复默认", LogLevel::BEHAVIOR);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"设为默认##berserk")) {
                {
                    std::lock_guard<std::mutex> lk(g_param_defaults_mutex);
                    g_param_defaults.berserk_lclick_ms = g_berserk_lclick_ms.load();
                    g_param_defaults.berserk_gap1_ms   = g_berserk_gap1_ms  .load();
                    g_param_defaults.berserk_mount_ms  = g_berserk_mount_ms .load();
                    g_param_defaults.berserk_gap2_ms   = g_berserk_gap2_ms  .load();
                }
                save_config();
                add_log(u8"[狂暴] 当前参数已保存为默认", LogLevel::BEHAVIOR);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(u8"测试")) {
            g_active_tab = 3;
            ImGui::Dummy(ImVec2(0, 8));

            // ---- 应用状态检测 ----
            ImGui::TextUnformatted(u8"应用状态");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 6));

            struct Check { const char* label; bool ok; };
            Check checks[] = {
                { u8"Interception 上下文", g_context != nullptr },
                { u8"活跃鼠标已识别",      active_mouse_id.load() != 0 },
                { u8"热键已配置",          [&]{
                    HotkeyConfig hk;
                    { std::lock_guard<std::mutex> lk(g_hotkey_mutex); hk = g_hotkey; }
                    bool has_vkey = false;
                    for (uint8_t vk : hk.vkeys) if (vk) { has_vkey = true; break; }
                    return hk.mouse_btn3 || hk.mouse_btn4 || hk.mouse_btn5 ||
                           hk.key_ctrl  || hk.key_shift   || hk.key_alt    || has_vkey;
                }() },
            };

            ImGui::Indent(8.0f);
            for (const auto& c : checks) {
                // 彩色圆灯
                ImVec2 p = ImGui::GetCursorScreenPos();
                float r = 6.0f;
                ImU32 col = c.ok ? IM_COL32(40, 180, 70, 255) : IM_COL32(210, 50, 50, 255);
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r, col);
                ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + 8, p.y));
                if (c.ok) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.55f, 0.25f, 1.0f));
                    ImGui::Text(u8"%s  ✓", c.label);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
                    ImGui::Text(u8"%s  ✗", c.label);
                }
                ImGui::PopStyleColor();
            }
            ImGui::Unindent(8.0f);

            ImGui::Dummy(ImVec2(0, 12));

            // ---- 按键激活检测 ----
            ImGui::TextUnformatted(u8"按键激活检测");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 6));

            static bool key_scanning = false;

            if (!key_scanning) {
                if (ImGui::Button(u8"开始检测")) key_scanning = true;
                ImGui::TextDisabled(u8"  点击后实时显示当前按下的按键");
            } else {
                if (ImGui::Button(u8"停止检测")) key_scanning = false;
            }

            if (key_scanning) {
                ImGui::Dummy(ImVec2(0, 6));

                // 收集当前所有按下的键名
                std::vector<std::string> active_keys;

                // 鼠标键
                if (GetAsyncKeyState(VK_LBUTTON)  & 0x8000) active_keys.push_back(u8"左键");
                if (GetAsyncKeyState(VK_RBUTTON)  & 0x8000) active_keys.push_back(u8"右键");
                if (GetAsyncKeyState(VK_MBUTTON)  & 0x8000) active_keys.push_back(u8"中键");
                if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) active_keys.push_back("Button4");
                if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) active_keys.push_back("Button5");

                // 修饰键(区分左右)
                if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) active_keys.push_back("Left Ctrl");
                if (GetAsyncKeyState(VK_RCONTROL) & 0x8000) active_keys.push_back("Right Ctrl");
                if (GetAsyncKeyState(VK_LSHIFT)   & 0x8000) active_keys.push_back("Left Shift");
                if (GetAsyncKeyState(VK_RSHIFT)   & 0x8000) active_keys.push_back("Right Shift");
                if (GetAsyncKeyState(VK_LMENU)    & 0x8000) active_keys.push_back("Left Alt");
                if (GetAsyncKeyState(VK_RMENU)    & 0x8000) active_keys.push_back("Right Alt");

                // 常用功能键
                if (GetAsyncKeyState(VK_RETURN) & 0x8000) active_keys.push_back("Enter");
                if (GetAsyncKeyState(VK_SPACE)  & 0x8000) active_keys.push_back("Space");
                if (GetAsyncKeyState(VK_TAB)    & 0x8000) active_keys.push_back("Tab");
                if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) active_keys.push_back("Esc");
                if (GetAsyncKeyState(VK_BACK)   & 0x8000) active_keys.push_back("Backspace");
                if (GetAsyncKeyState(VK_DELETE) & 0x8000) active_keys.push_back("Delete");
                if (GetAsyncKeyState(VK_INSERT) & 0x8000) active_keys.push_back("Insert");
                if (GetAsyncKeyState(VK_HOME)   & 0x8000) active_keys.push_back("Home");
                if (GetAsyncKeyState(VK_END)    & 0x8000) active_keys.push_back("End");
                if (GetAsyncKeyState(VK_PRIOR)  & 0x8000) active_keys.push_back("PgUp");
                if (GetAsyncKeyState(VK_NEXT)   & 0x8000) active_keys.push_back("PgDn");
                if (GetAsyncKeyState(VK_UP)     & 0x8000) active_keys.push_back(u8"↑");
                if (GetAsyncKeyState(VK_DOWN)   & 0x8000) active_keys.push_back(u8"↓");
                if (GetAsyncKeyState(VK_LEFT)   & 0x8000) active_keys.push_back(u8"←");
                if (GetAsyncKeyState(VK_RIGHT)  & 0x8000) active_keys.push_back(u8"→");

                // F 键
                for (int vk = VK_F1; vk <= VK_F24; ++vk) {
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        char buf[8]; snprintf(buf, sizeof(buf), "F%d", vk - VK_F1 + 1);
                        active_keys.push_back(buf);
                    }
                }

                // 数字行 + 字母
                for (int vk = '0'; vk <= 'Z'; ++vk) {
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        char buf[2] = { (char)vk, 0 };
                        active_keys.push_back(buf);
                    }
                }

                // 小键盘
                static const struct { int vk; const char* name; } numpad[] = {
                    {VK_NUMPAD0,"Num0"},{VK_NUMPAD1,"Num1"},{VK_NUMPAD2,"Num2"},
                    {VK_NUMPAD3,"Num3"},{VK_NUMPAD4,"Num4"},{VK_NUMPAD5,"Num5"},
                    {VK_NUMPAD6,"Num6"},{VK_NUMPAD7,"Num7"},{VK_NUMPAD8,"Num8"},
                    {VK_NUMPAD9,"Num9"},{VK_MULTIPLY,"Num*"},{VK_ADD,"Num+"},
                    {VK_SUBTRACT,"Num-"},{VK_DECIMAL,"Num."},{VK_DIVIDE,"Num/"},
                    {VK_RETURN /* numpad enter handled via VK_RETURN above */,nullptr},
                };
                for (const auto& n : numpad) {
                    if (n.name && (GetAsyncKeyState(n.vk) & 0x8000))
                        active_keys.push_back(n.name);
                }

                // 显示在子窗口标签框里
                ImGui::BeginChild("##key_display", ImVec2(0, 80), true);
                if (active_keys.empty()) {
                    ImGui::TextDisabled(u8"  （无按键按下）");
                } else {
                    std::string line;
                    for (size_t i = 0; i < active_keys.size(); ++i) {
                        if (i > 0) line += "  ";
                        line += active_keys[i];
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.50f, 0.90f, 1.0f));
                    ImGui::TextWrapped("%s", line.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::EndChild();
            }

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // ---- 底部固定按钮(开始/停止 分开,避免自动点击落在切换按钮上)----
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
    {
        const bool running = is_macro_running.load();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float btn_w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
        const ImVec2 btn_size(btn_w, 40);

        // 开始按钮(running 时禁用)
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.65f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.75f, 0.45f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.20f, 0.65f, 0.35f, 1.0f));
        ImGui::BeginDisabled(running);
        if (ImGui::Button(u8"开始", btn_size)) {
            if (active_mouse_id.load() == 0) {
                active_mouse_id.store(INTERCEPTION_MOUSE(0));
                add_log(u8"未检测到活跃鼠标,默认使用 MOUSE(0)", LogLevel::WARN);
            }
            is_macro_running.store(true);
            add_log(u8"[GUI] 开始", LogLevel::BEHAVIOR);
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // 停止按钮(未运行时禁用)
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.85f, 0.30f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.40f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.85f, 0.30f, 0.30f, 1.0f));
        ImGui::BeginDisabled(!running);
        if (ImGui::Button(u8"停止", btn_size)) {
            is_macro_running.store(false);
            add_log(u8"[GUI] 停止", LogLevel::BEHAVIOR);
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
    }

    // ---- 版权 ----
    {
        const char* copyright = u8"© 2026 北辰_kano. All rights reserved.";
        float text_w = ImGui::CalcTextSize(copyright).x;
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - text_w) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
        ImGui::TextUnformatted(copyright);
        ImGui::PopStyleColor();
    }

    ImGui::End();
    if (color_pushes > 0) ImGui::PopStyleColor(color_pushes);
}

} // namespace app
