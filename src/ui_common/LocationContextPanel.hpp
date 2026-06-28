#pragma once
// src/ui_common/LocationContextPanel.hpp
// Right 300-px context panel. Two tabs: LOCATION | MACRO SNAPSHOT.
// Updates on every GeoSelectionContext change and on live record ingest.
#include "Theme.hpp"
#include "../app/GeoSelectionContext.hpp"
#include "../providers/IDataProvider.hpp"
#include <imgui.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace macro {

struct LocStat { std::string label, value; };

class LocationContextPanel {
public:
    void update_context(const GeoSelectionContext& ctx) {
        ctx_   = ctx;
        stats_ = lookup(ctx);
    }

    void ingest_record(const NormalizedRecord& rec) {
        if (rec.geo.country_iso2 == ctx_.country_iso2) {
            if (rec.domain == "central_bank" || rec.domain == "monetary_policy")
                if (!rec.headline.empty())
                    live_rate_ = rec.headline.substr(0, 28);
        }
    }

    void render(float vp_h) {
        if (collapsed_) { render_collapsed(vp_h); return; }
        float px = ImGui::GetIO().DisplaySize.x - Theme::CONTEXT_PANEL_WIDTH;
        ImGui::SetNextWindowPos({px, 0.0f});
        ImGui::SetNextWindowSize({Theme::CONTEXT_PANEL_WIDTH,
                                  vp_h - Theme::STATUS_BAR_HEIGHT});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::BG_SECONDARY);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f, 10.0f});
        constexpr ImGuiWindowFlags F = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##CtxPnl", nullptr, F)) {
            ImGui::TextColored(Theme::TEXT_SECONDARY, "CONTEXT");
            ImGui::SameLine(Theme::CONTEXT_PANEL_WIDTH - 30.0f);
            if (ImGui::SmallButton("▸")) collapsed_ = true;
            if (ImGui::BeginTabBar("##CtxTabs")) {
                if (ImGui::BeginTabItem("LOCATION"))   { tab_location();  ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("MACRO SNAP")) { tab_macro();     ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("PROVIDERS"))  { tab_providers(); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    [[nodiscard]] float visible_width() const noexcept {
        return collapsed_ ? 20.0f : Theme::CONTEXT_PANEL_WIDTH;
    }

    // Provider health — written by Application
    int prov_total{0}, prov_ok{0}, prov_err{0};
    struct ProvStatus { std::string name; bool ok{true}; std::string note; };
    std::vector<ProvStatus> provider_statuses;

private:
    GeoSelectionContext ctx_;
    std::vector<LocStat> stats_;
    std::string live_rate_;
    bool collapsed_{false};

    void tab_location() {
        // Pill tags
        auto pill = [](const char* lbl) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4{0.23f,0.49f,0.65f,0.22f});
            ImGui::SmallButton(lbl);
            ImGui::PopStyleColor();
            ImGui::SameLine(0,4);
        };
        if (ctx_.resolution == GeoResolution::World) pill("GLOBAL");
        if (!ctx_.continent.empty()) pill(ctx_.continent.c_str());
        if (ctx_.is_g7)       pill("G7");
        if (ctx_.is_g20)      pill("G20");
        if (ctx_.is_eurozone) pill("EUROZONE");
        if (ctx_.is_em)       pill("EM");
        if (ctx_.is_nato)     pill("NATO");
        ImGui::NewLine(); ImGui::Spacing();

        ImGui::TextColored(Theme::TEXT_PRIMARY, "%s",
            ctx_.selected_name().c_str());
        ImGui::TextColored(Theme::TEXT_MUTED, "%s · Tier %d",
            ctx_.country_iso2.empty() ? "—" : ctx_.country_iso2.c_str(),
            fetch_tier(ctx_.resolution));
        ImGui::Separator(); ImGui::Spacing();

        // Stat cards 2-per-row
        float cw = (Theme::CONTEXT_PANEL_WIDTH - 28.0f) / 2.0f;
        auto card = [&](const char* lbl, const char* val) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_ELEVATED);
            ImGui::BeginChild(lbl, {cw, 46.0f}, true);
            ImGui::TextColored(Theme::TEXT_MUTED, "%s", lbl);
            ImGui::TextColored(Theme::TEXT_PRIMARY, "%s", val);
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };
        for (std::size_t i = 0; i+1 < stats_.size(); i += 2) {
            card(stats_[i].label.c_str(),   stats_[i].value.c_str());
            ImGui::SameLine(0, 6);
            card(stats_[i+1].label.c_str(), stats_[i+1].value.c_str());
        }
        if (stats_.size() % 2 == 1) {
            auto& s = stats_.back();
            card(s.label.c_str(), s.value.c_str());
        }

        ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(Theme::TEXT_MUTED, "BREADCRUMB");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Theme::CONTEXT_PANEL_WIDTH - 20.0f);
        ImGui::TextColored(Theme::TEXT_SECONDARY, "%s", ctx_.breadcrumb().c_str());
        ImGui::PopTextWrapPos();
    }

    void tab_macro() {
        ImGui::TextColored(Theme::TEXT_MUTED, "MACRO SNAPSHOT");
        ImGui::Spacing();
        auto row = [](const char* lbl, const char* val, const ImVec4& c) {
            ImGui::TextColored(Theme::TEXT_SECONDARY, "%-20s", lbl);
            ImGui::SameLine(160.0f);
            ImGui::TextColored(c, "%s", val);
        };
        row("Policy Rate",    live_rate_.empty() ? "—" : live_rate_.c_str(), Theme::TEXT_PRIMARY);
        row("GMSI Score",     "—",  Theme::SEV_ELEVATED);
        row("VIX",            "—",  Theme::TEXT_PRIMARY);
        row("US 10Y Real",    "—",  Theme::TEXT_PRIMARY);
        row("DXY",            "—",  Theme::TEXT_PRIMARY);
        ImGui::Spacing();
        ImGui::TextColored(Theme::TEXT_MUTED,
            "(Populates as providers ingest)");
    }

    void tab_providers() {
        ImGui::TextColored(Theme::TEXT_MUTED, "PROVIDER HEALTH");
        ImGui::Text("%d total  ", prov_total);
        ImGui::SameLine(); ImGui::TextColored(Theme::DIR_BULLISH, "%d ok", prov_ok);
        ImGui::SameLine(); ImGui::TextColored(Theme::SEV_CRITICAL," %d err", prov_err);
        ImGui::Separator();
        for (auto& p : provider_statuses) {
            ImGui::TextColored(p.ok ? Theme::DIR_BULLISH : Theme::SEV_CRITICAL,
                "●"); ImGui::SameLine(0,6);
            ImGui::TextColored(Theme::TEXT_SECONDARY, "%-18.18s", p.name.c_str());
            if (!p.note.empty()) {
                ImGui::SameLine(); ImGui::TextColored(Theme::TEXT_MUTED, "%s", p.note.c_str());
            }
        }
    }

    static std::vector<LocStat> lookup(const GeoSelectionContext& ctx) {
        static const std::unordered_map<std::string,
            std::vector<LocStat>> DB = {
            {"US", {{"Policy Rate","5.25–5.50%"},{"5Y CDS","22bps"},
                    {"Mkt Cap","$46.2T"},{"Exch Vol","$350B/d"}}},
            {"GB", {{"Policy Rate","5.25%"},{"5Y CDS","31bps"},
                    {"Mkt Cap","$3.1T"},{"Exch Vol","$28B/d"}}},
            {"DE", {{"Policy Rate","3.75%"},{"5Y CDS","29bps"},
                    {"Mkt Cap","$2.3T"},{"Exch Vol","$18B/d"}}},
            {"JP", {{"Policy Rate","-0.10%"},{"5Y CDS","26bps"},
                    {"Mkt Cap","$5.8T"},{"Exch Vol","$32B/d"}}},
            {"CN", {{"Policy Rate","3.45%"},{"5Y CDS","75bps"},
                    {"Mkt Cap","$9.8T"},{"Exch Vol","$42B/d"}}},
        };
        if (auto it = DB.find(ctx.country_iso2); it != DB.end())
            return it->second;
        return {{"Resolution",std::to_string(static_cast<int>(ctx.resolution))},
                {"Tier",std::to_string(fetch_tier(ctx.resolution))}};
    }

    void render_collapsed(float vp_h) {
        float px = ImGui::GetIO().DisplaySize.x - 20.0f;
        ImGui::SetNextWindowPos({px, 0.0f});
        ImGui::SetNextWindowSize({20.0f, 60.0f});
        constexpr ImGuiWindowFlags F = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::BG_SECONDARY);
        if (ImGui::Begin("##CtxC", nullptr, F)) {
            ImGui::SetCursorPosY(24.0f);
            if (ImGui::SmallButton("◂")) collapsed_ = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
        (void)vp_h;
    }
};

} // namespace macro
