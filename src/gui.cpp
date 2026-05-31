#include "gui.h"
#include "app_state.h"

#include <mutex>
#include <string>

#include "imgui.h"

namespace app {

static int g_active_tab = 0; // 0=首页 1=日志 2=测试

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
        ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + header_h);
        dl->AddRectFilled(p0, p1, IM_COL32(35, 90, 160, 255));

        float cx = p0.x + 30, cy = p0.y + header_h * 0.5f;
        dl->AddCircleFilled(ImVec2(cx, cy), 18.0f, IM_COL32(255, 255, 255, 255));
        dl->AddText(ImVec2(cx - 9, cy - 9), IM_COL32(35, 90, 160, 255), "BC");

        ImGui::SetCursorScreenPos(ImVec2(p0.x + 60, p0.y + 18));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextUnformatted(u8"北辰连点器 v1.0");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + header_h + 6));
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

} // namespace app
