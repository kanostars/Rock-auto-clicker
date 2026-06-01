#include "gui.h"
#include "core/app_state.h"

#include <chrono>
#include <mutex>
#include <string>

#include "imgui.h"

namespace app {

static int g_active_tab = 0; // 0=首页 1=日志 2=测试

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
        dl->AddRectFilled(p0, p1, IM_COL32(35, 90, 160, 255));

        float cx = p0.x + 30, cy = p0.y + header_h * 0.5f;
        dl->AddCircleFilled(ImVec2(cx, cy), 18.0f, IM_COL32(255, 255, 255, 255));
        dl->AddText(ImVec2(cx - 9, cy - 9), IM_COL32(35, 90, 160, 255), "BC");

        ImGui::SetCursorScreenPos(ImVec2(p0.x + 60, p0.y + 18));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextUnformatted(u8"连点器 v1.1");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

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
            ImGui::TextUnformatted(u8"按键时长");
            ImGui::Dummy(ImVec2(0, 4));

            int press_base = param_press_base_ms.load();
            if (ImGui::InputInt(u8"按下时长 (毫秒)", &press_base, 1, 5)) {
                if (press_base < 1)     press_base = 1;
                if (press_base > 60000) press_base = 60000;
                param_press_base_ms.store(press_base);
            }
            ImGui::TextDisabled(u8"  左键按下后持续时间的基础值");

            int press_jitter = param_press_jitter_ms.load();
            if (ImGui::InputInt(u8"按下随机延迟 (毫秒)", &press_jitter, 1, 5)) {
                if (press_jitter < 0)     press_jitter = 0;
                if (press_jitter > 60000) press_jitter = 60000;
                param_press_jitter_ms.store(press_jitter);
            }
            ImGui::TextDisabled(u8"  在基础值上额外加 0~该值 的随机抖动");
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
                add_log("未检测到活跃鼠标,默认使用 MOUSE(0)");
            }
            is_macro_running.store(true);
            add_log("[GUI] 开始");
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
            add_log("[GUI] 停止");
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
    }

    ImGui::End();
}

} // namespace app
