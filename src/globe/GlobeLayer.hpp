#pragma once
// src/globe/GlobeLayer.hpp
// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 — MACRO GLOBE + SATELLITE ANALYSIS MODEL
//
// Layout per reference images and spec:
//   Full-width viewport at top of terminal.
//   Left sub-panel: overlay toggles (Capital Flows, Sovereign Yields,
//                   Volatility Skew, Liquidity Nodes, CPI Differentials,
//                   FX Swap Lines) — directly from reference image.
//   Center: osgEarth WGS84 globe (FBO→ImGui::Image) with GMSI heat overlay,
//           graticule, admin boundaries, pulse markers, idle auto-rotation,
//           satellite layer switcher (NASA GIBS true-color, MODIS NDVI, FIRMS).
//   Right sub-panel: LOCATION CONTEXT — VIX/VIY, MOVE, SOFR 3M, US 10Y Real,
//                    USD/JPY, Net Liquidity — and ALERT STREAM (live alerts
//                    from provider feeds, severity-tagged).
//   Bottom strip: breadcrumb + overlay chip bar (Capital Flows [X], etc.)
//
// Integration (osgEarth + ImGui):
//   osgEarth renders into an FBO via osgViewer::GraphicsWindowEmbedded.
//   FBO color texture → ImGui::Image() in center pane.
//   Globe input events are intercepted in the child window and forwarded to
//   the OSG event queue only. No bleed to surrounding panels.
//   Drill-down state machine publishes GeoSelectionContext via AppStateBus.
// ─────────────────────────────────────────────────────────────────────────────
#include "../app/GeoSelectionContext.hpp"
#include "../app/AppStateBus.hpp"
#include "../globe/GMSIComputer.hpp"
#include "../providers/IDataProvider.hpp"
#include "../ui_common/Theme.hpp"
#include <imgui.h>
#include <glad/glad.h>
#include <array>
#include <chrono>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <vector>
#include <cmath>

#ifdef MACRO_HAVE_OSGEARTH
#include <osgEarth/MapNode>
#include <osgEarth/EarthManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/GraphicsWindow>
#endif

namespace macro {

// Live market stat shown in right LOCATION CONTEXT panel
struct GlobeMarketStat {
    std::string label;
    std::string value;
    float       change{0.0f};  // positive = above prior, negative = below
    bool        highlighted{false};
};

// Alert entry for the ALERT STREAM sub-panel
struct GlobeAlert {
    std::string text;
    int         severity{0};
    std::chrono::system_clock::time_point timestamp;
};

// Overlay layer toggle state
struct GlobeOverlay {
    const char* label;
    bool        active{false};
};

class GlobeLayer {
public:
    struct Config {
        int    width{1280};
        int    height{640};
        float  idle_rotation_rads_per_sec{0.03f};
        double idle_resume_after_sec{20.0};
        std::string boundary_representation{"de_jure"};
        std::string earth_file_path;
        // Layout proportions for the three sub-columns
        float left_panel_ratio{0.18f};   // overlay toggles
        float right_panel_ratio{0.20f};  // location context + alerts
    };

    explicit GlobeLayer(AppStateBus& bus, Config cfg = {})
        : bus_(bus), cfg_(std::move(cfg)) {
        init_fbo();
        seed_market_stats();
#ifdef MACRO_HAVE_OSGEARTH
        init_osgearth();
        std::println("[Globe] osgEarth renderer initialized");
#else
        std::println("[Globe] STUB renderer active (osgEarth pending)");
#endif
    }

    ~GlobeLayer() { cleanup_fbo(); }

    void render(float available_width, float available_height) {
        update_idle_rotation();

        float left_w   = available_width * cfg_.left_panel_ratio;
        float right_w  = available_width * cfg_.right_panel_ratio;
        float center_w = available_width - left_w - right_w;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {0.0f, 0.0f});
        ImGui::BeginChild("##GlobeRow", {available_width, available_height}, false,
                          ImGuiWindowFlags_NoScrollbar);

        // ── Left: overlay toggles ─────────────────────────────────────────
        render_overlay_panel(left_w, available_height);
        ImGui::SameLine(0, 0);

        // ── Center: globe viewport ────────────────────────────────────────
        render_globe_viewport(center_w, available_height);
        ImGui::SameLine(0, 0);

        // ── Right: location context + alert stream ────────────────────────
        render_context_panel(right_w, available_height);

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    // ── Drill-down API ───────────────────────────────────────────────────
    void drill_to_continent(const std::string& name, std::array<double,4> bbox) {
        ctx_.resolution = GeoResolution::Continent;
        ctx_.continent  = name;
        ctx_.bbox       = bbox;
        animate_camera_to(bbox);
        bus_.publish(ctx_);
        push_alert("Drill-down: " + name, 0);
    }

    void drill_to_country(const std::string& iso2, const std::string& display_name,
                          const std::string& iso3, std::array<double,4> bbox) {
        ctx_.resolution   = GeoResolution::Country;
        ctx_.country_iso2 = iso2;
        ctx_.country_iso3 = iso3;
        ctx_.country_name = display_name;
        ctx_.bbox         = bbox;
        animate_camera_to(bbox);
        bus_.publish(ctx_);
        push_alert("Selected country: " + display_name + " (" + iso2 + ")", 0);
    }

    void drill_up() {
        switch (ctx_.resolution) {
            case GeoResolution::Ground:
            case GeoResolution::City:
                ctx_.resolution = GeoResolution::State;
                ctx_.city_name.reset(); break;
            case GeoResolution::State:
                ctx_.resolution = GeoResolution::Country;
                ctx_.admin1_name.reset(); break;
            case GeoResolution::Country:
                ctx_.resolution = GeoResolution::Continent;
                ctx_.country_name.clear(); break;
            case GeoResolution::Continent:
                ctx_.resolution = GeoResolution::World;
                ctx_.continent.clear(); break;
            case GeoResolution::World: break;
        }
        bus_.publish(ctx_);
    }

    void push_alert(const std::string& text, int severity) {
        std::scoped_lock lk{alerts_mtx_};
        alerts_.push_front({text, severity, std::chrono::system_clock::now()});
        if (alerts_.size() > 12) alerts_.pop_back();
    }

    void update_market_stat(const std::string& label, const std::string& value, float chg = 0.0f) {
        for (auto& s : market_stats_) {
            if (s.label == label) {
                s.value = value;
                s.change = chg;
                return;
            }
        }
    }

    void ingest_record(const NormalizedRecord& rec) {
        // Feed GMSI computer
        gmsi_.ingest_record(rec);

        // High-severity records become alerts
        if (rec.severity >= 3)
            push_alert(rec.headline.substr(0, 72) + "…", rec.severity);
    }

    [[nodiscard]] const GeoSelectionContext& context() const noexcept { return ctx_; }

private:
    AppStateBus&        bus_;
    Config              cfg_;
    GeoSelectionContext ctx_;
    GMSIComputer        gmsi_;

    // FBO
    GLuint fbo_{0}, color_tex_{0}, depth_rb_{0};
    int    fbo_w_{0}, fbo_h_{0};

    // Idle rotation
    bool   is_rotating_{true};
    double idle_angle_{0.0};
    std::chrono::steady_clock::time_point last_interaction_{};
    std::chrono::steady_clock::time_point last_frame_time_{std::chrono::steady_clock::now()};

    // Animation
    bool   animating_{false};
    std::chrono::steady_clock::time_point anim_start_{};
    double anim_duration_ms_{800.0};

    // Overlay toggles (left panel — mirrors reference image)
    GlobeOverlay overlays_[6] = {
        {"Capital Flows",     true },
        {"Sovereign Yields",  false},
        {"Volatility Skew",   true },
        {"Liquidity Nodes",   false},
        {"CPI Differentials", false},
        {"FX Swap Lines",     false},
    };

    // Satellite layer toggles
    bool sat_true_color_{false};
    bool sat_ndvi_{false};
    bool sat_fires_{false};
    bool sat_sst_{false};

    // Right context panel
    std::vector<GlobeMarketStat> market_stats_;
    std::deque<GlobeAlert>       alerts_;
    std::mutex                   alerts_mtx_;

    // ── Left overlay panel ────────────────────────────────────────────────
    void render_overlay_panel(float w, float h) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_SECONDARY);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 8.0f});
        ImGui::BeginChild("##GlobeLeft", {w, h}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        // Section title
        ImGui::TextColored(Theme::TEXT_MUTED, "SECTION 1 — MACRO GLOBE");
        ImGui::Spacing();

        // Overlay toggle chips — match reference image style [X] label
        ImGui::TextColored(Theme::TEXT_MUTED, "OVERLAY LAYERS");
        ImGui::Spacing();
        for (auto& ov : overlays_) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ov.active ? Theme::ACCENT_BLUE_SOLID : Theme::BG_ELEVATED);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4{0.20f, 0.30f, 0.38f, 1.0f});
            std::string btn_label = std::string(ov.active ? "[X] " : "[ ] ") + ov.label;
            if (ImGui::Button(btn_label.c_str(), {w - 18.0f, 20.0f})) {
                ov.active = !ov.active;
            }
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        }

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(Theme::TEXT_MUTED, "SATELLITE LAYERS");
        ImGui::Spacing();

        auto sat_toggle = [&](const char* label, bool& state) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                state ? Theme::ACCENT_BLUE_SOLID : Theme::BG_ELEVATED);
            std::string lbl = std::string(state ? "[X] " : "[ ] ") + label;
            if (ImGui::Button(lbl.c_str(), {w - 18.0f, 20.0f}))
                state = !state;
            ImGui::PopStyleColor();
            ImGui::Spacing();
        };
        sat_toggle("True Color (VIIRS)",  sat_true_color_);
        sat_toggle("NDVI Vegetation",     sat_ndvi_);
        sat_toggle("Active Fires (FIRMS)",sat_fires_);
        sat_toggle("Sea Surface Temp",    sat_sst_);

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(Theme::TEXT_MUTED, "BOUNDARY MODE");
        ImGui::Spacing();
        ImGui::TextColored(Theme::ACCENT_CYAN_DIM, "  DE JURE (UN)");

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(Theme::TEXT_MUTED, "DRILL-DOWN");
        ImGui::Spacing();
        std::string breadcrumb = ctx_.breadcrumb();
        // Word-wrap breadcrumb
        float wrap_w = w - 18.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
        ImGui::TextColored(Theme::TEXT_SECONDARY, "%s", breadcrumb.c_str());
        ImGui::PopTextWrapPos();

        if (ctx_.resolution != GeoResolution::World) {
            ImGui::Spacing();
            if (ImGui::Button("◂ Up", {w - 18.0f, 20.0f}))
                drill_up();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── Center globe viewport ─────────────────────────────────────────────
    void render_globe_viewport(float w, float h) {
        // Resize FBO if needed
        if (static_cast<int>(w) != fbo_w_ || static_cast<int>(h - 48) != fbo_h_)
            resize_fbo(static_cast<int>(w), static_cast<int>(h - 48));

        render_to_fbo();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_PRIMARY);
        ImGui::BeginChild("##GlobeCenter", {w, h}, false,
                          ImGuiWindowFlags_NoScrollbar);

        // Globe image area
        float globe_display_h = h - 48.0f;

#ifdef MACRO_HAVE_OSGEARTH
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(color_tex_)),
                     {w, globe_display_h}, {0,1}, {1,0});
#else
        render_stub_globe(w, globe_display_h);
#endif

        // ── Breadcrumb overlay ─────────────────────────────────────────────
        ImVec2 globe_tl = ImGui::GetItemRectMin();
        ImDrawList* dl  = ImGui::GetWindowDrawList();
        {
            std::string bc = ctx_.breadcrumb();
            ImVec2 bcp = {globe_tl.x + 10.0f, globe_tl.y + 8.0f};
            dl->AddRectFilled({bcp.x - 4, bcp.y - 2},
                {bcp.x + ImGui::CalcTextSize(bc.c_str()).x + 8, bcp.y + 16},
                IM_COL32(10,14,20,180), 3.0f);
            dl->AddText(bcp, ImGui::ColorConvertFloat4ToU32(Theme::TEXT_SECONDARY),
                        bc.c_str());
        }

        // Input capture (globe-isolated)
        ImVec2 globe_rect_min = globe_tl;
        ImVec2 globe_rect_max = {globe_tl.x + w, globe_tl.y + globe_display_h};
        bool hovering_globe = ImGui::IsMouseHoveringRect(globe_rect_min, globe_rect_max);
        if (hovering_globe) {
            if (ImGui::GetIO().MouseWheel != 0.0f ||
                ImGui::IsMouseDragging(0)         ||
                ImGui::IsMouseClicked(0)) {
                is_rotating_      = false;
                last_interaction_ = std::chrono::steady_clock::now();
            }
        }

        // ── Bottom overlay chip strip ──────────────────────────────────────
        ImGui::SetCursorPosY(globe_display_h + 4.0f);
        ImGui::SetCursorPosX(8.0f);
        for (int i = 0; i < 6; ++i) {
            auto& ov = overlays_[i];
            if (ov.active)
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::ACCENT_BLUE_SOLID);
            else
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::BG_ELEVATED);

            std::string chip = std::string(ov.active ? "[X] " : "[ ] ") + ov.label;
            if (ImGui::SmallButton(chip.c_str())) ov.active = !ov.active;
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 6);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── Stub globe renderer (no osgEarth) ────────────────────────────────
    void render_stub_globe(float w, float h) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wpos    = ImGui::GetWindowPos();
        float  yoff    = ImGui::GetCursorPosY();

        float cx = wpos.x + w * 0.5f;
        float cy = wpos.y + yoff + h * 0.5f;
        float r  = std::min(w, h) * 0.42f;

        // Deep space background
        dl->AddRectFilled({wpos.x, wpos.y + yoff},
                          {wpos.x + w, wpos.y + yoff + h},
                          IM_COL32(4, 8, 16, 255));

        // Atmosphere glow ring
        for (int i = 5; i >= 1; --i) {
            dl->AddCircle({cx, cy}, r + i * 3.0f,
                ImGui::ColorConvertFloat4ToU32(
                    {0.18f, 0.83f, 1.0f, 0.03f * (6 - i)}),
                128, static_cast<float>(i));
        }

        // Globe disc
        dl->AddCircleFilled({cx, cy}, r, IM_COL32(10, 22, 45, 255), 128);

        // Graticule latitude lines
        for (int lat = -60; lat <= 60; lat += 30) {
            float lat_rad  = lat * 3.14159f / 180.0f;
            float yr       = r * std::sin(lat_rad);
            float xr       = std::sqrt(std::max(0.0f, r * r - yr * yr));
            float thick    = (lat == 0) ? 0.8f : 0.4f;
            ImU32 col      = (lat == 0)
                ? IM_COL32(45, 212, 255, 40)
                : IM_COL32(45, 212, 255, 18);
            dl->AddLine({cx - xr, cy - yr}, {cx + xr, cy - yr}, col, thick);
        }

        // Graticule longitude lines (spinning with idle_angle)
        for (int i = 0; i < 12; ++i) {
            double ang  = idle_angle_ + i * (3.14159265358979 / 6.0);
            float  cos_ = static_cast<float>(std::cos(ang));
            float  sin_ = static_cast<float>(std::sin(ang));
            // Elliptical longitude line via polyline approximation
            const int SEGS = 48;
            ImVec2 pts[SEGS + 1];
            for (int s = 0; s <= SEGS; ++s) {
                float t     = static_cast<float>(s) / SEGS;
                float theta = t * 2.0f * 3.14159f;
                float lx    = r * cos_ * std::cos(theta);
                float ly    = r * std::sin(theta);
                pts[s] = {cx + lx, cy + ly};
            }
            dl->AddPolyline(pts, SEGS + 1,
                IM_COL32(45, 212, 255, 15), 0, 0.4f);
        }

        // GMSI heat pulse markers for top-stress countries
        struct HotspotDef {
            const char* iso2; float screen_x_frac; float screen_y_frac; int severity;
        };
        static constexpr HotspotDef HOTSPOTS[] = {
            {"UA", 0.565f, 0.38f, 5},  // Ukraine
            {"RU", 0.580f, 0.30f, 4},  // Russia
            {"IR", 0.596f, 0.46f, 4},  // Iran
            {"AR", 0.378f, 0.65f, 3},  // Argentina
            {"TR", 0.578f, 0.41f, 3},  // Turkey
            {"NG", 0.518f, 0.53f, 3},  // Nigeria
            {"CN", 0.693f, 0.40f, 2},  // China
            {"US", 0.262f, 0.38f, 1},  // US
        };

        // Pulse animation
        float pulse_t = std::fmod(
            std::chrono::duration<float>(
                std::chrono::steady_clock::now().time_since_epoch()).count(),
            2.0f) / 2.0f;

        for (const auto& hs : HOTSPOTS) {
            GMSIScore score = gmsi_.score(hs.iso2);
            float actual_sev = (score.score > 0.1f) ? score.score : hs.severity;

            float hx = wpos.x + hs.screen_x_frac * w;
            float hy = wpos.y + yoff + hs.screen_y_frac * h;

            ImVec4 col4 = Theme::severity_color(static_cast<int>(actual_sev));
            ImU32  col  = ImGui::ColorConvertFloat4ToU32(col4);

            // Pulsing rings
            if (hs.severity >= 3) {
                float ring_r = 4.0f + pulse_t * 14.0f;
                float ring_a = (1.0f - pulse_t) * 0.6f;
                dl->AddCircle({hx, hy}, ring_r,
                    ImGui::ColorConvertFloat4ToU32({col4.x, col4.y, col4.z, ring_a}),
                    32, 1.2f);
            }
            // Core dot
            dl->AddCircleFilled({hx, hy}, 3.5f, col, 16);
        }

        // Outer ring
        dl->AddCircle({cx, cy}, r, IM_COL32(45, 212, 255, 55), 128, 1.0f);

        // Advance cursor past the drawn region
        ImGui::Dummy({w, h});
    }

    // ── Right context panel ───────────────────────────────────────────────
    void render_context_panel(float w, float h) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_SECONDARY);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 8.0f});
        ImGui::BeginChild("##GlobeRight", {w, h}, true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        // Header — matches reference: "LOCATION CONTEXT: NY / LND"
        std::string loc_label = "LOCATION CONTEXT";
        if (ctx_.resolution != GeoResolution::World && !ctx_.country_name.empty())
            loc_label = "LOCATION CONTEXT: " + ctx_.country_name;
        else
            loc_label = "LOCATION CONTEXT: NY / LND";
        ImGui::TextColored(Theme::TEXT_MUTED, "%s", loc_label.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        // Market stats (matches reference image rows exactly)
        for (auto& stat : market_stats_) {
            ImGui::TextColored(Theme::TEXT_SECONDARY, "%-18s", stat.label.c_str());
            ImGui::SameLine(w * 0.52f);
            ImVec4 val_col = stat.highlighted
                ? (stat.change >= 0 ? Theme::DIR_BULLISH : Theme::DIR_BEARISH)
                : Theme::TEXT_PRIMARY;
            ImGui::TextColored(val_col, "%s", stat.value.c_str());
            // GMSI score inline for country selection
            if (ctx_.resolution == GeoResolution::Country && stat.label == "GMSI") {
                GMSIScore sc = gmsi_.score(ctx_.country_iso2);
                ImGui::SameLine();
                ImGui::TextColored(Theme::severity_color(static_cast<int>(sc.score)),
                    "  %.1f/5", sc.score);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ALERT STREAM — matches reference image section
        ImGui::TextColored(Theme::TEXT_MUTED, "ALERT STREAM");
        ImGui::Spacing();

        float alerts_h = h - ImGui::GetCursorPosY() - 8.0f;
        ImGui::BeginChild("##Alerts", {w - 16.0f, alerts_h}, false,
                          ImGuiWindowFlags_NoScrollbar);
        {
            std::scoped_lock lk{alerts_mtx_};
            for (const auto& alert : alerts_) {
                ImGui::TextColored(Theme::severity_color(alert.severity),
                    "> %s", alert.text.c_str());
            }
        }
        if (alerts_.empty()) {
            ImGui::TextColored(Theme::TEXT_MUTED, "> Monitoring active...");
        }
        ImGui::EndChild();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── FBO management ────────────────────────────────────────────────────
    void init_fbo() {
        fbo_w_ = cfg_.width;
        fbo_h_ = cfg_.height;
        create_fbo_objects(fbo_w_, fbo_h_);
    }

    void resize_fbo(int w, int h) {
        if (w == fbo_w_ && h == fbo_h_) return;
        cleanup_fbo();
        fbo_w_ = w; fbo_h_ = h;
        create_fbo_objects(w, h);
    }

    void create_fbo_objects(int w, int h) {
        if (w <= 0 || h <= 0) return;
        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glGenTextures(1, &color_tex_);
        glBindTexture(GL_TEXTURE_2D, color_tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);
        glGenRenderbuffers(1, &depth_rb_);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, depth_rb_);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void cleanup_fbo() {
        if (fbo_)       glDeleteFramebuffers(1, &fbo_);
        if (color_tex_) glDeleteTextures(1, &color_tex_);
        if (depth_rb_)  glDeleteRenderbuffers(1, &depth_rb_);
        fbo_ = color_tex_ = depth_rb_ = 0;
    }

    void render_to_fbo() {
        if (!fbo_) return;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, fbo_w_, fbo_h_);
        glClearColor(0.039f, 0.055f, 0.078f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#ifdef MACRO_HAVE_OSGEARTH
        if (viewer_) viewer_->frame();
#endif
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void update_idle_rotation() {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_frame_time_).count();
        last_frame_time_ = now;
        double idle_s = std::chrono::duration<double>(now - last_interaction_).count();
        if (!is_rotating_ && idle_s > cfg_.idle_resume_after_sec)
            is_rotating_ = true;
        if (is_rotating_)
            idle_angle_ += cfg_.idle_rotation_rads_per_sec * dt;
    }

    void animate_camera_to([[maybe_unused]] std::array<double,4> bbox) {
        animating_        = true;
        anim_start_       = std::chrono::steady_clock::now();
        is_rotating_      = false;
        last_interaction_ = std::chrono::steady_clock::now();
#ifdef MACRO_HAVE_OSGEARTH
        if (earth_manip_) {
            double lon = (bbox[0] + bbox[2]) / 2.0;
            double lat = (bbox[1] + bbox[3]) / 2.0;
            osgEarth::Viewpoint vp;
            vp.focalPoint() = osgEarth::GeoPoint(
                osgEarth::SpatialReference::get("wgs84"), lon, lat, 0.0);
            double span = std::max(bbox[2]-bbox[0], bbox[3]-bbox[1]);
            vp.range() = span * 111000.0;
            earth_manip_->setViewpoint(vp, anim_duration_ms_ / 1000.0);
        }
#endif
    }

    void seed_market_stats() {
        market_stats_ = {
            {"VIX / VIY",    "13.3",  0.0f, true},
            {"MOVE Index",   "94.8",  0.0f, false},
            {"SOFR 3M",      "5.11%", 0.0f, true},
            {"US 10Y Real",  "2.18%", 0.0f, true},
            {"USD/JPY",      "152.4", 0.0f, false},
            {"Net Liquidity","$5.4T", 0.0f, false},
            {"GMSI",         "—",     0.0f, false},
        };
        alerts_.push_back({"NY Open Volatility Snap Detected", 2,
            std::chrono::system_clock::now()});
        alerts_.push_back({"Vanna flows accelerating in ITS", 1,
            std::chrono::system_clock::now()});
        alerts_.push_back({"Rates volatility stabilising", 0,
            std::chrono::system_clock::now()});
    }

#ifdef MACRO_HAVE_OSGEARTH
    void init_osgearth() {
        viewer_ = std::make_unique<osgViewer::Viewer>();
        osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> gwe =
            new osgViewer::GraphicsWindowEmbedded(0, 0, fbo_w_, fbo_h_);
        osg::ref_ptr<osg::Camera> cam = viewer_->getCamera();
        cam->setGraphicsContext(gwe);
        cam->setViewport(new osg::Viewport(0, 0, fbo_w_, fbo_h_));
        cam->setClearColor(osg::Vec4(0.039f, 0.055f, 0.078f, 1.0f));
        osg::ref_ptr<osg::Node> earth_node = osgDB::readNodeFile(
            cfg_.earth_file_path.empty() ? "data/world.earth" : cfg_.earth_file_path);
        if (earth_node) {
            viewer_->setSceneData(earth_node);
            earth_manip_ = new osgEarth::Util::EarthManipulator();
            viewer_->setCameraManipulator(earth_manip_);
        }
        viewer_->setThreadingModel(osgViewer::Viewer::SingleThreaded);
        viewer_->realize();
    }
    std::unique_ptr<osgViewer::Viewer>             viewer_;
    osg::ref_ptr<osgEarth::Util::EarthManipulator> earth_manip_;
#endif
};

} // namespace macro
