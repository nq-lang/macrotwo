#pragma once
// src/ui_common/StatusBar.hpp
// Bottom 24-px persistent status bar.
#include "Theme.hpp"
#include <imgui.h>
#include <atomic>
#include <chrono>
#include <ctime>
#include <format>

namespace macro {

struct SourceHealth {
    std::atomic<int> total{0}, ok{0}, err{0};
    void reset() { total=0; ok=0; err=0; }
};

class StatusBar {
public:
    explicit StatusBar(SourceHealth& h) : h_(h) {}

    void render(float vp_w) {
        const float y = ImGui::GetIO().DisplaySize.y - Theme::STATUS_BAR_HEIGHT;
        ImGui::SetNextWindowPos({0.0f, y});
        ImGui::SetNextWindowSize({vp_w, Theme::STATUS_BAR_HEIGHT});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::BG_SECONDARY);
        ImGui::PushStyleColor(ImGuiCol_Border,   Theme::BORDER_SUBTLE);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f,3.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        constexpr ImGuiWindowFlags F = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoMove   | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##SB", nullptr, F)) {
            // Left: source counts
            ImGui::SetCursorPosY(ImGui::GetCursorPosY()+2.0f);
            ImGui::TextColored(Theme::TEXT_SECONDARY, "%d sources", h_.total.load());
            ImGui::SameLine(0,12);
            ImGui::TextColored(Theme::DIR_BULLISH,  "● %d ok",  h_.ok.load());
            ImGui::SameLine(0,12);
            int e = h_.err.load();
            ImGui::TextColored(e>0 ? Theme::SEV_CRITICAL : Theme::TEXT_MUTED,
                               "● %d err", e);

            // Centre: UTC clock
            {
                auto now = std::chrono::system_clock::now();
                auto tt  = std::chrono::system_clock::to_time_t(now);
                std::tm gm{};
#ifdef _WIN32
                gmtime_s(&gm,&tt);
#else
                gmtime_r(&tt,&gm);
#endif
                char tbuf[20]; std::strftime(tbuf,sizeof(tbuf),"%H:%M:%S UTC",&gm);
                float tw = ImGui::CalcTextSize(tbuf).x;
                ImGui::SameLine(vp_w/2.0f - tw/2.0f);
                ImGui::TextColored(Theme::TEXT_MUTED, "%s", tbuf);
            }

            // Right: severity legend
            static constexpr struct {const char* lbl; const ImVec4* col;} CHIPS[] = {
                {"INFO",    &Theme::SEV_INFORMATIONAL},
                {"LOW",     &Theme::SEV_LOW},
                {"ELEVATED",&Theme::SEV_ELEVATED},
                {"HIGH",    &Theme::SEV_HIGH},
                {"CRITICAL",&Theme::SEV_CRITICAL},
                {"SYSTEMIC",&Theme::SEV_SYSTEMIC},
            };
            float rx = vp_w - 8.0f;
            for (int i=5;i>=0;--i) {
                float cw = ImGui::CalcTextSize(CHIPS[i].lbl).x + 18.0f;
                rx -= cw;
                ImGui::SameLine(rx);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                float th = ImGui::GetTextLineHeight();
                dl->AddRectFilled({p.x,p.y-1},{p.x+cw-4,p.y+th+1},
                    ImGui::ColorConvertFloat4ToU32(
                        {CHIPS[i].col->x,CHIPS[i].col->y,CHIPS[i].col->z,0.18f}),
                    Theme::ROUNDING_CHIP);
                ImGui::TextColored(*CHIPS[i].col,"● %s",CHIPS[i].lbl);
                rx -= 6.0f;
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

private:
    SourceHealth& h_;
};

} // namespace macro
