#pragma once
// src/tables/TablesLayer.hpp  — Section 2 remodel v0.3
// ─────────────────────────────────────────────────────────────────────────────
// EVENT-DRIVEN GEO-SCOPED INTELLIGENCE FEED
//
// Behaviour
// ─────────────────────────────────────────────────────────────────────────────
// • Subscribes to AppStateBus.  On every GeoSelectionContext change the new
//   context is stored but no fetch is issued yet.
//
// • tick() is called once per frame (by Application::tick, before render).
//   It checks whether the stored context has been stable for ≥ DEBOUNCE_MS
//   (2 000 ms).  If so it calls GeoScopedFetcher::request_fetch(), which
//   enqueues the request for the background worker.
//
// • While the debounce is counting down a cyan amber progress bar fills
//   left→right over 2 s.  While an async fetch is in progress the bar
//   shows an indeterminate sliding pulse.
//
// • On fetch completion, FetchResult objects are drained per-frame and
//   forwarded to the correct FeedModule:
//     is_loading == true  → module spinner
//     is_error   == true  → module error chip
//     otherwise           → module.replace_articles()
//
// • Background pipeline NormalizedRecords (from ProviderEngine) are
//   continuously ingested via ingest() and prepended to the matching
//   FeedModule without clearing the geo-fetched articles.
//
// • Satellite zoom (GeoResolution::Ground) suppresses all fetches.
//
// Layout
// ─────────────────────────────────────────────────────────────────────────────
//  ┌─ Section header bar (22 px) ──────────────────────────────────────────┐
//  │ SECTION 2 — MACRO INTELLIGENCE & NEWS STREAM  Scope:[X]  Tier:[Y]    │
//  ├─ 2-px progress / pulse bar ───────────────────────────────────────────┤
//  │ Scrollable child window:                                               │
//  │   [FeedModule 0] MACROECONOMIC DEVELOPMENTS                           │
//  │   [FeedModule 1] MICROECONOMIC DEVELOPMENTS                           │
//  │   [FeedModule 2] GEOPOLITICAL & GEO-TENSIONS                         │
//  │   [FeedModule 3] CENTRAL BANK UPDATES                                 │
//  │   [FeedModule 4] MONETARY POLICY                                      │
//  │   [FeedModule 5] GLOBAL / REGIONAL NEWS                               │
//  │   [FeedModule 6] MILITARY & WAR NEWS                                  │
//  └────────────────────────────────────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────
#include "FeedModule.hpp"
#include "GeoScopedFetcher.hpp"
#include "ArticleRecord.hpp"
#include "../app/AppStateBus.hpp"
#include "../app/Secrets.hpp"
#include "../providers/IDataProvider.hpp"
#include "../ui_common/Theme.hpp"
#include <imgui.h>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

namespace macro {

class TablesLayer {
public:
    static constexpr int DEBOUNCE_MS = 2000;

    TablesLayer(AppStateBus& bus, const Secrets& sec)
        : bus_(bus)
        , fetcher_(std::make_unique<GeoScopedFetcher>(sec))
    {
        for (int d = 0; d < FEED_DOMAIN_COUNT; ++d)
            mods_[d] = std::make_unique<FeedModule>(static_cast<FeedDomain>(d));

        bus_tok_ = bus_.subscribe([this](const GeoSelectionContext& ctx){
            on_context(ctx);
        });
        fetcher_->start();
    }

    ~TablesLayer() {
        bus_.unsubscribe(bus_tok_);
        fetcher_->stop();
    }

    // ── Per-frame logic (call BEFORE render) ─────────────────────────────
    void tick() {
        // Debounce gate
        if (pending_.has_value()) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - change_at_).count();
            if (ms >= DEBOUNCE_MS) {
                fetcher_->request_fetch(*pending_);
                current_      = *pending_;
                pending_.reset();
                fetching_     = true;
                tier_label_   = tier_str(fetch_tier(current_.resolution));
            }
        }

        // Drain fetch results
        for (auto& r : fetcher_->drain_results()) {
            int di = static_cast<int>(r.domain);
            if (di < 0 || di >= FEED_DOMAIN_COUNT) continue;
            auto& m = *mods_[di];
            if      (r.is_loading) m.set_loading(true, r.fetch_tier_label);
            else if (r.is_error)   m.set_error(r.error_msg);
            else { m.replace_articles(std::move(r.articles)); m.set_loading(false); }
        }

        if (fetching_ && !fetcher_->is_fetching()) fetching_ = false;
    }

    // ── Background pipeline ingest ────────────────────────────────────────
    void ingest(const NormalizedRecord& rec) {
        ArticleRecord art = to_article(rec);
        if (art.headline.empty()) return;
        int di = static_cast<int>(route(rec));
        mods_[di]->prepend_article(std::move(art));
    }

    // ── Render ────────────────────────────────────────────────────────────
    void render(float x, float y, float width, float height) {
        ImGui::SetNextWindowPos({x, y});
        ImGui::SetNextWindowSize({width, height});
        ImGui::SetNextWindowBgAlpha(1.0f);

        constexpr ImGuiWindowFlags F =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize         |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar   |
            ImGuiWindowFlags_NoSavedSettings                            |
            ImGuiWindowFlags_NoBringToDisplayFrontOnFocus;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::BG_PRIMARY);
        if (ImGui::Begin("##S2Feed", nullptr, F)) {
            render_header(width - 16.0f);
            render_bar   (width - 16.0f);

            const float scroll_h = height - hdr_h() - 4.0f;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_PRIMARY);
            ImGui::BeginChild("##FeedScroll", {width - 8.0f, scroll_h});
            float fw = ImGui::GetContentRegionAvail().x;
            for (int d = 0; d < FEED_DOMAIN_COUNT; ++d)
                mods_[d]->render(fw);
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // Legacy shim — no-op
    void update_geo_scope(const std::string&) {}

private:
    AppStateBus&       bus_;
    AppStateBus::Token bus_tok_{};

    std::unique_ptr<GeoScopedFetcher>                   fetcher_;
    std::array<std::unique_ptr<FeedModule>, FEED_DOMAIN_COUNT> mods_;

    // Debounce state
    std::optional<GeoSelectionContext>    pending_;
    std::chrono::steady_clock::time_point change_at_{};
    GeoSelectionContext                   current_;
    bool                                  fetching_{false};
    std::string                           tier_label_{"GLOBAL"};

    // ── AppStateBus callback ──────────────────────────────────────────────
    void on_context(const GeoSelectionContext& ctx) {
        if (ctx.resolution == GeoResolution::Ground) return;  // satellite — suppress
        pending_   = ctx;
        change_at_ = std::chrono::steady_clock::now();
        fetching_  = true;  // show bar immediately
        // Push loading sentinels so modules show spinner right away
        for (int d = 0; d < FEED_DOMAIN_COUNT; ++d)
            mods_[d]->set_loading(true, tier_str(fetch_tier(ctx.resolution)));
    }

    // ── Section header ────────────────────────────────────────────────────
    static float hdr_h() noexcept { return 46.0f; }

    void render_header(float width) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_SECONDARY);
        ImGui::BeginChild("##S2Hdr", {width, 22.0f}, false,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPosY(3.0f);
        ImGui::SetCursorPosX(8.0f);
        ImGui::TextColored(Theme::ACCENT_CYAN_DIM,
            "SECTION 2 — MACROECONOMIC INTELLIGENCE & NEWS STREAM");

        ImGui::SameLine(width - 290.0f);
        ImGui::TextColored(Theme::TEXT_MUTED, "Scope:");
        ImGui::SameLine(0,4);
        ImGui::TextColored(Theme::ACCENT_CYAN, "%s",
            current_.selected_name().c_str());
        ImGui::SameLine(0,8);
        ImGui::TextColored(Theme::TEXT_MUTED, "Tier:");
        ImGui::SameLine(0,4);
        ImGui::TextColored(Theme::ACCENT_CYAN_DIM, "[%s]", tier_label_.c_str());
        ImGui::SameLine(0,14);

        if (!fetching_ && !pending_.has_value()) {
            ImGui::TextColored(Theme::DIR_BULLISH, "● LIVE");
        } else if (pending_.has_value()) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - change_at_).count();
            int rem = static_cast<int>(std::max(0LL,
                static_cast<long long>(DEBOUNCE_MS) - ms));
            ImGui::TextColored(Theme::SEV_ELEVATED,
                "◉ REFRESHING IN %dms", rem);
        } else {
            ImGui::TextColored(Theme::SEV_ELEVATED, "◉ FETCHING…");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void render_bar(float width) {
        ImDrawList* dl  = ImGui::GetWindowDrawList();
        ImVec2      p   = ImGui::GetCursorScreenPos();
        const float bh  = 2.0f;

        // Track
        dl->AddRectFilled({p.x,p.y},{p.x+width,p.y+bh}, IM_COL32(42,50,64,180));

        if (fetching_) {
            if (pending_.has_value()) {
                // Countdown fill — amber
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - change_at_).count();
                float prog = std::clamp(static_cast<float>(ms) / DEBOUNCE_MS, 0.0f, 1.0f);
                dl->AddRectFilled({p.x,p.y},{p.x+width*prog,p.y+bh},
                    ImGui::ColorConvertFloat4ToU32(Theme::SEV_ELEVATED));
            } else {
                // Indeterminate pulse — cyan
                float t = std::fmod(
                    std::chrono::duration<float>(
                        std::chrono::steady_clock::now().time_since_epoch()).count(),
                    1.6f) / 1.6f;
                float s = std::max(0.0f, t - 0.30f);
                float e = std::min(1.0f, t + 0.30f);
                dl->AddRectFilled({p.x+width*s,p.y},{p.x+width*e,p.y+bh},
                    ImGui::ColorConvertFloat4ToU32(Theme::ACCENT_CYAN));
            }
        }
        ImGui::Dummy({width, bh + 2.0f});
    }

    // ── Domain routing ────────────────────────────────────────────────────
    static FeedDomain route(const NormalizedRecord& rec) {
        const auto& d = rec.domain;
        if (d == "econ_calendar")   return FeedDomain::MacroDevelopments;
        if (d == "monetary_policy") return FeedDomain::MonetaryPolicy;
        if (d == "central_bank")    return FeedDomain::CentralBankUpdates;
        if (d == "geopolitics")     return FeedDomain::GeopoliticalTensions;
        if (d == "sector_data" || d == "positioning")
                                    return FeedDomain::MicroDevelopments;
        // "news" — keyword routing
        const auto& h = rec.headline;
        auto ci = [&](const char* kw){ return h.find(kw) != std::string::npos; };
        if (ci("militar") || ci("war")   || ci("conflict") ||
            ci("troop")   || ci("attack") || rec.severity >= 4)
            return FeedDomain::MilitaryWarNews;
        if (ci("central bank") || ci("ECB") || ci("Fed ") || ci("BOE") || ci("BOJ"))
            return FeedDomain::CentralBankUpdates;
        if (ci("rate")  || ci("bps") || ci("hike") || ci("cut"))
            return FeedDomain::MonetaryPolicy;
        if (ci("GDP") || ci("inflation") || ci("CPI") || ci("employment") || ci("NFP"))
            return FeedDomain::MacroDevelopments;
        if (ci("earnings") || ci("revenue") || ci("sector") || ci("company"))
            return FeedDomain::MicroDevelopments;
        return FeedDomain::GlobalRegionalNews;
    }

    static ArticleRecord to_article(const NormalizedRecord& rec) {
        ArticleRecord a;
        a.domain         = route(rec);
        a.source_name    = rec.source_name;
        a.headline       = rec.headline;
        // Try to extract snippet from payload JSON "description" or "summary"
        try {
            auto j = nlohmann::json::parse(rec.payload_json);
            for (const char* k : {"description","summary","notes","text"}) {
                if (j.contains(k) && j[k].is_string()) {
                    a.snippet = j[k].get<std::string>().substr(0, 300);
                    break;
                }
            }
        } catch (...) {}
        a.id             = rec.record_id;
        a.geo_label      = rec.geo.country_iso2.value_or("Global");
        a.fetch_tier_label = "PIPELINE";
        a.is_geo_fetched = false;
        a.severity       = rec.severity;
        a.published_at   = rec.timestamp;
        return a;
    }

    static std::string tier_str(int t) noexcept {
        switch (t) {
            case 0: return "GLOBAL";
            case 1: return "CONTINENT";
            case 2: return "COUNTRY";
            case 3: return "LOCAL";
            default:return "SATELLITE";
        }
    }
};

} // namespace macro
