#pragma once
// src/ui_common/FilterRail.hpp
// Left 220-px collapsible domain/overlay filter rail.
#include "Theme.hpp"
#include <imgui.h>
#include <atomic>
#include <array>
#include <string_view>
#include <cstring>
#include <cctype>

namespace macro {

enum DomainBit : uint32_t {
    DOMAIN_MACRO        = 1u << 0,
    DOMAIN_MICRO        = 1u << 1,
    DOMAIN_GEOPOLITICS  = 1u << 2,
    DOMAIN_CENTRAL_BANK = 1u << 3,
    DOMAIN_MONETARY     = 1u << 4,
    DOMAIN_NEWS         = 1u << 5,
    DOMAIN_MILITARY     = 1u << 6,
    OVERLAY_BORDERS     = 1u << 7,
    OVERLAY_HEAT        = 1u << 8,
    OVERLAY_SATELLITE   = 1u << 9,
    OVERLAY_GRID        = 1u << 10,
    DOMAIN_ALL          = 0x7FFu,
};

struct DomainEntry { std::string_view label; DomainBit bit; int count{0}; };

class FilterRail {
public:
    FilterRail() : mask_(DOMAIN_ALL) {}

    [[nodiscard]] uint32_t enabled_mask() const noexcept {
        return mask_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool is_enabled(DomainBit b) const noexcept {
        return (enabled_mask() & b) != 0;
    }
    void set_count(DomainBit b, int c) {
        for (auto& d : domains_) if (d.bit == b) { d.count = c; return; }
        for (auto& d : overlays_) if (d.bit == b) { d.count = c; return; }
    }

    void render(float viewport_h) {
        if (collapsed_) { render_collapsed(); return; }
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({Theme::FILTER_RAIL_WIDTH, viewport_h - Theme::STATUS_BAR_HEIGHT});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::BG_SECONDARY);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f, 10.0f});
        constexpr ImGuiWindowFlags F = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##FilterRail", nullptr, F)) {
            ImGui::TextColored(Theme::TEXT_MUTED, "FILTERS");
            ImGui::SameLine(Theme::FILTER_RAIL_WIDTH - 36.0f);
            if (ImGui::SmallButton("◂")) collapsed_ = true;
            ImGui::Separator();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::BG_ELEVATED);
            ImGui::InputText("##srch", sbuf_, sizeof(sbuf_));
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::TextColored(Theme::TEXT_MUTED, "FEED DOMAINS");
            render_tree(domains_, 7);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(Theme::TEXT_MUTED, "GLOBE OVERLAYS");
            render_tree(overlays_, 4);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(Theme::TEXT_MUTED, "BOUNDARY");
            ImGui::TextColored(Theme::ACCENT_CYAN_DIM, "  DE JURE (UN)");
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    [[nodiscard]] float visible_width() const noexcept {
        return collapsed_ ? 24.0f : Theme::FILTER_RAIL_WIDTH;
    }

private:
    std::atomic<uint32_t> mask_;
    bool  collapsed_{false};
    char  sbuf_[64]{};

    DomainEntry domains_[7] = {
        {"Macro Developments",   DOMAIN_MACRO},
        {"Micro Developments",   DOMAIN_MICRO},
        {"Geopolitical Tensions",DOMAIN_GEOPOLITICS},
        {"Central Bank Updates", DOMAIN_CENTRAL_BANK},
        {"Monetary Policy",      DOMAIN_MONETARY},
        {"Global / Regional",    DOMAIN_NEWS},
        {"Military & War",       DOMAIN_MILITARY},
    };
    DomainEntry overlays_[4] = {
        {"Admin Boundaries", OVERLAY_BORDERS},
        {"GMSI Heat Overlay",OVERLAY_HEAT},
        {"Satellite Imagery",OVERLAY_SATELLITE},
        {"Admin Gridlines",  OVERLAY_GRID},
    };

    void render_tree(DomainEntry* entries, int n) {
        uint32_t mask = mask_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            auto& d = entries[i];
            if (sbuf_[0]) {
                char lo[64]{}, sl[64]{};
                for (int k=0;k<63&&d.label[k];++k) lo[k]=char(std::tolower(d.label[k]));
                for (int k=0;k<63&&sbuf_[k];  ++k) sl[k]=char(std::tolower(sbuf_[k]));
                if (!std::strstr(lo,sl)) continue;
            }
            bool on = (mask & d.bit) != 0;
            ImGui::PushID(i + static_cast<int>(d.bit));
            if (ImGui::Checkbox("##cb", &on)) {
                if (on) mask |= d.bit; else mask &= ~d.bit;
                mask_.store(mask, std::memory_order_relaxed);
            }
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::TextColored(on ? Theme::TEXT_PRIMARY : Theme::TEXT_MUTED,
                               "%s", d.label.data());
            if (d.count > 0) { ImGui::SameLine();
                ImGui::TextColored(Theme::ACCENT_CYAN_DIM, " %d", d.count); }
        }
    }

    void render_collapsed() {
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({24.0f, 80.0f});
        constexpr ImGuiWindowFlags F = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::BG_SECONDARY);
        if (ImGui::Begin("##FRC", nullptr, F)) {
            ImGui::SetCursorPosY(30.0f);
            if (ImGui::SmallButton("▸")) collapsed_ = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
};

} // namespace macro
