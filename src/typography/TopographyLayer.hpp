#pragma once
// src/topography/TopographyLayer.hpp
// ─────────────────────────────────────────────────────────────────────────────
// SECTION 3 — MACRO NEWS SECTOR IMPACT TOPOGRAPHY
//
// Layout per reference images:
//   Top strip: event selector bar (e.g. "NFP +250K [Hot Labor Market]")
//   3D Surface: full-width OpenGL heightfield mesh (GL_PATCHES tessellation)
//               rendered into FBO → ImGui::Image
//               Legend: RED = Bearish Sector Exposure, GREEN = Bullish Sector Exposure
//               Label: "[MODEL: MACRO SENSITIVITY 3D SURFACE]"
//               Mesh is jagged/faceted (per reference image) — NOT smooth
//   Below surface — two tables side-by-side:
//     Left:  "Sector Exposure to Selected Macro Event"
//            GICS Sector | Beta to Event | Projected Impact
//     Right: "Macro Risk Drivers"
//            Driver | Current Level | Threshold Limit
//   + Regime Conviction Table (full width):
//     Sector | Region | Regime Score | Direction | Conviction | Key Drivers | LLM Rationale
//   + Single-Name Search bar at bottom
// ─────────────────────────────────────────────────────────────────────────────
#include "FactorModel.hpp"
#include "LLMRationaleService.hpp"
#include "../app/AppStateBus.hpp"
#include "../ui_common/Theme.hpp"
#include <glad/glad.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <print>
#include <cmath>

namespace macro {

// Macro event for the event selector bar
struct MacroEvent {
    std::string label;
    std::string description;
    bool        active{false};
};

// Macro risk driver for the right sub-table
struct MacroRiskDriver {
    std::string driver;
    std::string current_level;
    std::string threshold_limit;
    bool        breached{false};
};

// Sector exposure row for the left sub-table
struct SectorExposure {
    std::string gics_sector;
    float       beta_to_event{0.0f};
    float       projected_impact{0.0f};
};

class TopographyLayer {
public:
    enum class Region { US, Europe, Both };

    struct Controls {
        Region region{Region::US};
        int    breadth_n{150};
        bool   sector_active[N_SECTORS];
        Controls() { std::fill(sector_active, sector_active + N_SECTORS, true); }
    };

    explicit TopographyLayer(AppStateBus& bus, std::string anthropic_key)
        : bus_(bus)
        , llm_(std::move(anthropic_key)) {
        init_gl();
        llm_.start();

        bus_token_ = bus_.subscribe([this](const GeoSelectionContext& ctx) {
            if (ctx.continent == "Europe") controls_.region = Region::Europe;
            else if (ctx.continent == "North America") controls_.region = Region::US;
            geo_ctx_ = ctx;
        });

        // Seed macro events matching reference image
        seed_macro_events();
        seed_risk_drivers();
        seed_sector_exposures();

        model_.recompute();
        rebuild_terrain_mesh();
        request_all_rationales();
    }

    ~TopographyLayer() {
        bus_.unsubscribe(bus_token_);
        llm_.stop();
        cleanup_gl();
    }

    void ingest(const NormalizedRecord& rec) {
        // Update factor model from sector ETF data
        if (rec.domain == "sector_data") {
            try {
                auto j = nlohmann::json::parse(rec.payload_json);
                float chg = j.value("change_percentage", 0.0f);
                static const std::unordered_map<std::string,int> ETF = {
                    {"XLE",0},{"XLB",1},{"XLI",2},{"XLY",3},{"XLP",4},
                    {"XLV",5},{"XLF",6},{"XLK",7},{"XLC",8},{"XLU",9},{"XLRE",10}
                };
                std::string sym = j.value("symbol","");
                auto it = ETF.find(sym);
                if (it != ETF.end()) {
                    live_score_updates_[it->second] = std::clamp(chg / 3.0f, -3.0f, 3.0f);
                    needs_recompute_ = true;
                }
            } catch (...) {}
        }
        // Build news digest for LLM rationale
        if (rec.domain == "news" || rec.domain == "econ_calendar") {
            std::scoped_lock lk{digest_mtx_};
            news_digest_ += rec.headline.substr(0, 70) + "; ";
            if (news_digest_.size() > 800)
                news_digest_ = news_digest_.substr(news_digest_.size() - 800);
        }
    }

    void render(float x, float y, float width, float height) {
        llm_.dispatch_pending_results();

        if (needs_recompute_) {
            model_.recompute();
            rebuild_terrain_mesh();
            request_all_rationales();
            needs_recompute_ = false;
        }

        ImGui::SetNextWindowPos({x, y});
        ImGui::SetNextWindowSize({width, height});
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoResize        |
            ImGuiWindowFlags_NoCollapse      |
            ImGuiWindowFlags_NoTitleBar      |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::BG_PRIMARY);
        if (ImGui::Begin("##TopoLayer", nullptr, flags)) {

            // ── Section 3 header ─────────────────────────────────────────
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_SECONDARY);
            ImGui::BeginChild("##S3Hdr", {width - 16.0f, 22.0f}, false,
                              ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPosX(8.0f); ImGui::SetCursorPosY(3.0f);
            ImGui::TextColored(Theme::ACCENT_CYAN_DIM,
                "SECTION 3 — MACRO NEWS SECTOR IMPACT TOPOGRAPHY");
            ImGui::EndChild();
            ImGui::PopStyleColor();

            float used_h = 26.0f;
            float remain = height - used_h;

            // ── Event selector bar ───────────────────────────────────────
            render_event_bar(width - 16.0f);
            used_h += 28.0f; remain -= 28.0f;

            // ── 3D surface viewport (52% of remaining height) ─────────────
            float terrain_h = remain * 0.42f;
            render_terrain_viewport(width - 16.0f, terrain_h);
            used_h += terrain_h; remain -= terrain_h;

            // ── Dual sub-tables (sector exposure + risk drivers) ──────────
            float sub_table_h = remain * 0.32f;
            render_sub_tables(width - 16.0f, sub_table_h);
            remain -= sub_table_h;

            // ── Regime conviction table ───────────────────────────────────
            float regime_h = remain - 36.0f;
            render_regime_conviction_table(width - 16.0f, regime_h);

            // ── Single-name search ────────────────────────────────────────
            render_single_name_search(width - 16.0f);
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

private:
    AppStateBus&        bus_;
    AppStateBus::Token  bus_token_{};
    GeoSelectionContext geo_ctx_;
    FactorModel         model_;
    LLMRationaleService llm_;
    Controls            controls_;

    std::unordered_map<std::string, RegimeRationale> rationales_;
    std::mutex digest_mtx_;
    std::string news_digest_;
    float live_score_updates_[N_SECTORS]{};
    std::atomic<bool> needs_recompute_{false};

    // Section 3 data
    std::vector<MacroEvent>     macro_events_;
    std::vector<SectorExposure> sector_exposures_;
    std::vector<MacroRiskDriver>risk_drivers_;
    int active_event_idx_{0};

    // Single-name search
    char  search_buf_[64]{};
    std::string selected_name_;

    // GL terrain
    GLuint terrain_fbo_{0}, terrain_tex_{0}, terrain_depth_{0};
    GLuint terrain_vao_{0}, terrain_vbo_{0}, terrain_ebo_{0};
    GLuint terrain_shader_{0};
    int    fbo_w_{1200}, fbo_h_{300};

    struct TerrainVertex { float x, y, z, score; };
    std::vector<TerrainVertex> terrain_verts_;
    std::vector<uint32_t>      terrain_indices_;

    // ── Event selector bar ────────────────────────────────────────────────
    void render_event_bar(float width) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_ELEVATED);
        ImGui::BeginChild("##EventBar", {width, 26.0f}, false,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPosY(4.0f);
        ImGui::SetCursorPosX(8.0f);
        for (int i = 0; i < static_cast<int>(macro_events_.size()); ++i) {
            bool sel = (i == active_event_idx_);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, Theme::ACCENT_BLUE_SOLID);
            else     ImGui::PushStyleColor(ImGuiCol_Button, Theme::BG_SECONDARY);
            if (ImGui::SmallButton(macro_events_[i].label.c_str())) {
                active_event_idx_ = i;
                update_sector_exposures_for_event(i);
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 6);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── Terrain 3D viewport ───────────────────────────────────────────────
    void render_terrain_viewport(float width, float height) {
        int iw = static_cast<int>(width);
        int ih = static_cast<int>(height) - 40;
        if (ih < 10) ih = 10;
        resize_terrain_fbo(iw, ih);
        render_terrain_to_fbo(iw, ih);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_PRIMARY);
        ImGui::BeginChild("##TopoViewport", {width, height}, false,
                          ImGuiWindowFlags_NoScrollbar);

        // Model label (matches reference "[MODEL: MACRO SENSITIVITY 3D SURFACE]")
        ImGui::TextColored(Theme::ACCENT_CYAN_DIM, "[MODEL: MACRO SENSITIVITY 3D SURFACE]");
        ImGui::SameLine(0, 16);
        ImGui::TextColored(Theme::DIR_BEARISH,  "■ RED = Bearish Sector Exposure");
        ImGui::SameLine(0, 12);
        ImGui::TextColored(Theme::DIR_BULLISH, "■ GREEN = Bullish Sector Exposure");

        // Terrain image
        float img_h = height - 36.0f;
        ImGui::Image(
            reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(terrain_tex_)),
            {width, img_h}, {0, 1}, {1, 0});

        // Sector axis labels overlay
        ImVec2 img_min = ImGui::GetItemRectMin();
        ImDrawList* dl  = ImGui::GetWindowDrawList();
        for (int s = 0; s < N_SECTORS; ++s) {
            if (!controls_.sector_active[s]) continue;
            float xf = (static_cast<float>(s) + 0.5f) / N_SECTORS;
            float lx = img_min.x + xf * width;
            float ly = img_min.y + img_h - 14.0f;
            dl->AddText({lx - 18.0f, ly},
                ImGui::ColorConvertFloat4ToU32(Theme::TEXT_MUTED),
                std::string(GICS_SECTORS[static_cast<std::size_t>(s)]).substr(0, 7).c_str());
        }

        // Controls row
        render_terrain_controls(width);

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void render_terrain_controls(float width) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_SECONDARY);
        ImGui::BeginChild("##TerrainCtrl", {width, 24.0f}, false,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPosY(3.0f);
        ImGui::SetCursorPosX(6.0f);

        // Region buttons
        static constexpr const char* RGN[] = {"US","Europe","Both"};
        for (int i = 0; i < 3; ++i) {
            bool sel = (static_cast<int>(controls_.region) == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, Theme::ACCENT_BLUE_SOLID);
            else     ImGui::PushStyleColor(ImGuiCol_Button, Theme::BG_ELEVATED);
            if (ImGui::SmallButton(RGN[i])) {
                controls_.region = static_cast<Region>(i);
                needs_recompute_ = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 4);
        }
        ImGui::SameLine(0, 12);

        // Breadth slider
        ImGui::SetNextItemWidth(100.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::BG_ELEVATED);
        if (ImGui::SliderInt("##BreadthN", &controls_.breadth_n, 10, 300))
            needs_recompute_ = true;
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 4);
        ImGui::TextColored(Theme::TEXT_MUTED, "Top-%d names", controls_.breadth_n);
        ImGui::SameLine(0, 12);

        // Sector chips
        for (int s = 0; s < N_SECTORS; ++s) {
            bool& on = controls_.sector_active[s];
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, Theme::BG_ELEVATED);
            else    ImGui::PushStyleColor(ImGuiCol_Button, Theme::BG_PRIMARY);
            if (ImGui::SmallButton(std::string(GICS_SECTORS[s]).substr(0,4).c_str())) {
                on = !on;
                needs_recompute_ = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 3);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── Dual sub-tables ───────────────────────────────────────────────────
    void render_sub_tables(float width, float height) {
        float half = (width - 8.0f) * 0.5f;

        // ── Left: Sector Exposure to Selected Macro Event ─────────────────
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_PANEL);
        ImGui::BeginChild("##SectorExp", {half, height}, true,
                          ImGuiWindowFlags_NoScrollbar);

        std::string ev_label = (active_event_idx_ < static_cast<int>(macro_events_.size()))
            ? macro_events_[active_event_idx_].description
            : "Selected Macro Event";
        ImGui::TextColored(Theme::TEXT_SECONDARY, "Sector Exposure to %s", ev_label.c_str());
        ImGui::Spacing();

        // Header
        ImGui::TextColored(Theme::TEXT_MUTED, "%-22s  %-16s  %s",
            "GICS SECTOR", "BETA TO EVENT", "PROJECTED IMPACT");
        ImGui::Separator();

        for (const auto& se : sector_exposures_) {
            ImGui::TextColored(Theme::TEXT_PRIMARY, "%-22s", se.gics_sector.c_str());
            ImGui::SameLine(160.0f);
            ImVec4 beta_col = se.beta_to_event < 0 ? Theme::DIR_BEARISH : Theme::DIR_BULLISH;
            ImGui::TextColored(beta_col, "%+.2fx", se.beta_to_event);
            ImGui::SameLine(240.0f);
            ImVec4 imp_col  = se.projected_impact < 0 ? Theme::DIR_BEARISH : Theme::DIR_BULLISH;
            ImGui::TextColored(imp_col, "%+.2f%%", se.projected_impact);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 8);

        // ── Right: Macro Risk Drivers ─────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_PANEL);
        ImGui::BeginChild("##RiskDrivers", {half, height}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::TextColored(Theme::TEXT_SECONDARY, "Macro Risk Drivers");
        ImGui::Spacing();
        ImGui::TextColored(Theme::TEXT_MUTED, "%-20s  %-16s  %s",
            "DRIVER", "CURRENT LEVEL", "THRESHOLD LIMIT");
        ImGui::Separator();
        for (const auto& rd : risk_drivers_) {
            ImGui::TextColored(Theme::TEXT_PRIMARY, "%-20s", rd.driver.c_str());
            ImGui::SameLine(154.0f);
            ImVec4 lv_col = rd.breached ? Theme::SEV_CRITICAL : Theme::TEXT_PRIMARY;
            ImGui::TextColored(lv_col, "%-16s", rd.current_level.c_str());
            ImGui::SameLine(254.0f);
            ImGui::TextColored(rd.breached ? Theme::SEV_ELEVATED : Theme::TEXT_MUTED,
                "%s", rd.threshold_limit.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── Regime conviction table ───────────────────────────────────────────
    void render_regime_conviction_table(float width, float height) {
        const auto& results = (controls_.region == Region::Europe)
            ? model_.results_eu() : model_.results_us();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_PANEL);
        ImGui::BeginChild("##RegimeConv", {width, height}, true);

        ImGui::TextColored(Theme::TEXT_SECONDARY,
            "Regime Conviction Table — %s  (%d names · Eigen OLS · LLM: claude-sonnet-4-6)",
            controls_.region == Region::US ? "US" : "Europe",
            controls_.breadth_n);

        ImGuiTableFlags tfl =
            ImGuiTableFlags_ScrollY       |
            ImGuiTableFlags_RowBg         |
            ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_Resizable     |
            ImGuiTableFlags_Sortable;

        if (ImGui::BeginTable("##RegimeTbl", 7, tfl,
                              {width - 8.0f, height - 36.0f})) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("SECTOR",      ImGuiTableColumnFlags_WidthFixed, 108);
            ImGui::TableSetupColumn("REGION",      ImGuiTableColumnFlags_WidthFixed,  64);
            ImGui::TableSetupColumn("SCORE",       ImGuiTableColumnFlags_WidthFixed,  60);
            ImGui::TableSetupColumn("DIRECTION",   ImGuiTableColumnFlags_WidthFixed,  85);
            ImGui::TableSetupColumn("CONVICTION",  ImGuiTableColumnFlags_WidthFixed,  80);
            ImGui::TableSetupColumn("KEY DRIVERS", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("LLM RATIONALE", ImGuiTableColumnFlags_WidthStretch, 0);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(results.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    if (!controls_.sector_active[i]) continue;
                    const auto& r = results[static_cast<std::size_t>(i)];
                    std::string key = std::format("{}:{}", r.sector, r.region);
                    auto it = rationales_.find(key);

                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::TextColored(Theme::TEXT_PRIMARY, "%s", r.sector.data());

                    ImGui::TableNextColumn();
                    ImGui::TextColored(Theme::TEXT_MUTED, "%s", r.region.data());

                    ImGui::TableNextColumn();
                    int dir_int = r.regime_score > 0.3f ? 1 : r.regime_score < -0.3f ? -1 : 0;
                    ImGui::TextColored(Theme::direction_color(dir_int), "%+.2f", r.regime_score);

                    ImGui::TableNextColumn();
                    if (it != rationales_.end()) {
                        int d = (it->second.direction == "bullish") ? 1
                              : (it->second.direction == "bearish") ? -1 : 0;
                        ImGui::TextColored(Theme::direction_color(d),
                            "%s %s",
                            d > 0 ? "▲" : d < 0 ? "▼" : "—",
                            it->second.direction.c_str());
                    } else {
                        ImGui::TextColored(Theme::TEXT_MUTED, "…");
                    }

                    ImGui::TableNextColumn();
                    if (it != rationales_.end()) {
                        const auto& cv = it->second.conviction;
                        ImVec4 cc = cv == "high"   ? Theme::CONVICTION_HIGH
                                  : cv == "medium" ? Theme::CONVICTION_MEDIUM
                                  : Theme::CONVICTION_LOW;
                        ImGui::TextColored(cc, "%s", cv.c_str());
                    }

                    ImGui::TableNextColumn();
                    if (it != rationales_.end()) {
                        ImGui::TextColored(Theme::TEXT_SECONDARY,
                            "%s · %s · %s",
                            it->second.key_drivers[0].c_str(),
                            it->second.key_drivers[1].c_str(),
                            it->second.key_drivers[2].c_str());
                    }

                    ImGui::TableNextColumn();
                    if (it != rationales_.end()) {
                        bool from_llm = it->second.from_llm;
                        ImGui::TextColored(
                            from_llm ? Theme::TEXT_PRIMARY : Theme::TEXT_SECONDARY,
                            "%s%s",
                            from_llm ? "" : "[auto] ",
                            it->second.rationale.c_str());
                    }
                }
            }
            clipper.End();
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── Single-name search ────────────────────────────────────────────────
    void render_single_name_search(float width) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_SECONDARY);
        ImGui::BeginChild("##NameSearch", {width, 32.0f}, false,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPosY(5.0f);
        ImGui::SetCursorPosX(8.0f);
        ImGui::TextColored(Theme::TEXT_MUTED, "SINGLE NAME");
        ImGui::SameLine(0, 12);
        ImGui::SetNextItemWidth(240.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::BG_ELEVATED);
        ImGui::InputTextWithHint("##NSInput", "ticker or company name…",
                                  search_buf_, sizeof(search_buf_));
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 8);
        if (ImGui::SmallButton("SEARCH") && search_buf_[0] != '\0')
            selected_name_ = search_buf_;
        ImGui::SameLine(0, 16);
        ImGui::TextColored(Theme::TEXT_MUTED,
            "Fuzzy match · %d-name universe · Factor betas + sector divergence flag",
            controls_.breadth_n);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── GL terrain mesh ───────────────────────────────────────────────────
    void init_gl() {
        create_terrain_fbo(fbo_w_, fbo_h_);
        compile_terrain_shader();
        glGenVertexArrays(1, &terrain_vao_);
        glGenBuffers(1, &terrain_vbo_);
        glGenBuffers(1, &terrain_ebo_);
        std::println("[Topography] GL initialized");
    }

    void cleanup_gl() {
        if (terrain_shader_) glDeleteProgram(terrain_shader_);
        if (terrain_vao_)    glDeleteVertexArrays(1, &terrain_vao_);
        if (terrain_vbo_)    glDeleteBuffers(1, &terrain_vbo_);
        if (terrain_ebo_)    glDeleteBuffers(1, &terrain_ebo_);
        if (terrain_fbo_)    glDeleteFramebuffers(1, &terrain_fbo_);
        if (terrain_tex_)    glDeleteTextures(1, &terrain_tex_);
        if (terrain_depth_)  glDeleteRenderbuffers(1, &terrain_depth_);
    }

    void create_terrain_fbo(int w, int h) {
        glGenFramebuffers(1, &terrain_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, terrain_fbo_);
        glGenTextures(1, &terrain_tex_);
        glBindTexture(GL_TEXTURE_2D, terrain_tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, terrain_tex_, 0);
        glGenRenderbuffers(1, &terrain_depth_);
        glBindRenderbuffer(GL_RENDERBUFFER, terrain_depth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, terrain_depth_);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void resize_terrain_fbo(int w, int h) {
        if (w == fbo_w_ && h == fbo_h_) return;
        if (terrain_fbo_)    glDeleteFramebuffers(1, &terrain_fbo_);
        if (terrain_tex_)    glDeleteTextures(1, &terrain_tex_);
        if (terrain_depth_)  glDeleteRenderbuffers(1, &terrain_depth_);
        terrain_fbo_ = terrain_tex_ = terrain_depth_ = 0;
        fbo_w_ = w; fbo_h_ = h;
        create_terrain_fbo(w, h);
    }

    void compile_terrain_shader() {
        // Vertex: positions + score, oblique MVP projection
        const char* vert = R"glsl(
#version 460 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aScore;
out float vScore;
out float vHeight;
out vec3 vNormal;
uniform mat4 uMVP;
void main() {
    vScore  = aScore;
    vHeight = aPos.y;
    vNormal = vec3(0.0, 1.0, 0.0); // updated per-face in geometry shader if needed
    gl_Position = uMVP * vec4(aPos, 1.0);
})glsl";

        // Fragment: §2 muted bear/bull palette with height shading
        // Red (bearish) ↔ Green (bullish) matching reference image
        const char* frag = R"glsl(
#version 460 core
in float vScore;
in float vHeight;
out vec4 FragColor;

// §2 muted palette: terracotta bear, slate-teal bull, amber mid
vec3 bear_deep   = vec3(0.55, 0.22, 0.16);  // deep rust
vec3 bear_mid    = vec3(0.71, 0.35, 0.29);  // terracotta
vec3 neutral     = vec3(0.70, 0.68, 0.42);  // muted amber
vec3 bull_mid    = vec3(0.30, 0.49, 0.47);  // slate-teal
vec3 bull_bright = vec3(0.40, 0.65, 0.55);  // brighter teal

void main() {
    // Map score [-3,3] → [0,1]
    float t = clamp((vScore + 3.0) / 6.0, 0.0, 1.0);
    vec3 col;
    if      (t < 0.25) col = mix(bear_deep,   bear_mid, t * 4.0);
    else if (t < 0.50) col = mix(bear_mid,    neutral,  (t - 0.25) * 4.0);
    else if (t < 0.75) col = mix(neutral,     bull_mid, (t - 0.50) * 4.0);
    else               col = mix(bull_mid,    bull_bright, (t - 0.75) * 4.0);

    // Height-based ambient: higher peaks brighter
    float shade = 0.55 + 0.45 * clamp((vHeight + 0.6) / 1.2, 0.0, 1.0);
    // Faceted wireframe darkening at edges (based on derivative — approximated)
    float edge  = clamp(abs(fwidth(vHeight)) * 8.0, 0.0, 0.35);
    FragColor = vec4(col * shade * (1.0 - edge * 0.5), 1.0);
})glsl";

        auto compile = [&](GLenum type, const char* src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
                std::println("[Topography] shader compile error: {}", log);
            }
            return s;
        };

        GLuint vs = compile(GL_VERTEX_SHADER,   vert);
        GLuint fs = compile(GL_FRAGMENT_SHADER, frag);
        terrain_shader_ = glCreateProgram();
        glAttachShader(terrain_shader_, vs);
        glAttachShader(terrain_shader_, fs);
        glLinkProgram(terrain_shader_);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    void rebuild_terrain_mesh() {
        const auto& results = (controls_.region == Region::Europe)
            ? model_.results_eu() : model_.results_us();
        if (results.empty()) return;

        static constexpr int MESH_COLS = 48;  // more columns = jaggier peaks
        auto grid = model_.build_terrain(results, MESH_COLS);

        terrain_verts_.clear();
        terrain_indices_.clear();

        int rows = static_cast<int>(grid.rows());
        int cols = static_cast<int>(grid.cols());

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float xf = static_cast<float>(r) / (rows - 1) * 2.0f - 1.0f;
                float zf = static_cast<float>(c) / (cols - 1) * 2.0f - 1.0f;
                float h  = grid[static_cast<std::size_t>(r),
                                 static_cast<std::size_t>(c)] * 0.35f;
                float sc = results[static_cast<std::size_t>(r)].regime_score;
                terrain_verts_.push_back({xf, h, zf, sc});
            }
        }

        for (int r = 0; r < rows - 1; ++r) {
            for (int c = 0; c < cols - 1; ++c) {
                uint32_t tl = static_cast<uint32_t>(r * cols + c);
                terrain_indices_.insert(terrain_indices_.end(),
                    {tl, tl + static_cast<uint32_t>(cols), tl + 1,
                     tl + 1, tl + static_cast<uint32_t>(cols),
                     tl + static_cast<uint32_t>(cols) + 1});
            }
        }

        glBindVertexArray(terrain_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, terrain_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(terrain_verts_.size() * sizeof(TerrainVertex)),
            terrain_verts_.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrain_ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(terrain_indices_.size() * sizeof(uint32_t)),
            terrain_indices_.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
            reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    void render_terrain_to_fbo(int w, int h) {
        if (terrain_verts_.empty() || !terrain_fbo_) return;
        glBindFramebuffer(GL_FRAMEBUFFER, terrain_fbo_);
        glViewport(0, 0, w, h);
        glClearColor(0.039f, 0.055f, 0.078f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        glUseProgram(terrain_shader_);
        // Oblique projection matrix — gives the angled 3D view seen in reference
        static const float MVP[16] = {
             0.85f,  0.0f,  0.0f, 0.0f,
             0.0f,   0.95f, 0.0f, 0.0f,
            -0.35f,  0.40f,-0.60f,0.0f,
             0.0f,  -0.25f, 0.0f, 1.0f
        };
        GLint mvp_loc = glGetUniformLocation(terrain_shader_, "uMVP");
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, MVP);

        glBindVertexArray(terrain_vao_);
        glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(terrain_indices_.size()),
            GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ── Rationale requests ────────────────────────────────────────────────
    void request_all_rationales() {
        std::string digest;
        { std::scoped_lock lk{digest_mtx_}; digest = news_digest_; }
        auto req = [&](const std::vector<RegimeResult>& results) {
            for (auto& r : results) {
                std::string key = std::format("{}:{}", r.sector, r.region);
                llm_.request(r, digest, [this, key](RegimeRationale rat) {
                    rationales_[key] = std::move(rat);
                });
            }
        };
        req(model_.results_us());
        req(model_.results_eu());
    }

    // ── Seed data (matches reference image values) ────────────────────────
    void seed_macro_events() {
        macro_events_ = {
            {"NFP +250K [Hot Labor Market]",   "NFP +250K Hot Labor Market"},
            {"CPI Above Est [Sticky Infl.]",   "CPI Above Estimate Sticky Inflation"},
            {"Fed Hike +25bps [Hawkish]",      "Federal Reserve Rate Hike +25bps"},
            {"OPEC+ Cut [Oil Supply Shock]",   "OPEC+ Production Cut Supply Shock"},
            {"China PMI Miss [Growth Risk]",   "China Manufacturing PMI Miss"},
        };
        macro_events_[0].active = true;
        active_event_idx_ = 0;
    }

    void seed_sector_exposures() {
        sector_exposures_ = {
            {"Information Technology (XLK)",  -1.81f, -2.04f},
            {"Financials (XLF)",               0.87f,  1.37f},
            {"Energy (XLE)",                   0.30f,  0.40f},
            {"Real Estate (XLRE)",            -1.79f, -1.33f},
            {"Utilities (XLU)",               -0.42f, -0.68f},
            {"Consumer Staples (XLP)",         0.15f,  0.22f},
        };
    }

    void seed_risk_drivers() {
        risk_drivers_ = {
            {"10Y Yields",      "4.10%",  "4.50%", false},
            {"USD Index (DXY)", "103.6",  "105.5", false},
            {"WTI Crude",       "82.8",   "90.00", false},
            {"VIX",             "13.3",   "20.0",  false},
            {"MOVE Index",      "94.8",   "120.0", false},
            {"HY-IG Spread",    "142 bps","180 bps",false},
        };
    }

    void update_sector_exposures_for_event(int event_idx) {
        // Rotate exposures to simulate different macro event sensitivities
        if (event_idx == 0) { // NFP hot
            sector_exposures_[0].beta_to_event = -1.81f;
            sector_exposures_[1].beta_to_event = +0.87f;
        } else if (event_idx == 1) { // CPI above
            sector_exposures_[0].beta_to_event = -2.10f;
            sector_exposures_[1].beta_to_event = +0.45f;
        } else if (event_idx == 2) { // Fed hike
            sector_exposures_[0].beta_to_event = -2.40f;
            sector_exposures_[1].beta_to_event = +1.10f;
        }
        needs_recompute_ = true;
    }
};

} // namespace macro
