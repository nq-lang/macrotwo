#pragma once
// src/tables/FeedModule.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Single vertically-stacked feed module for Section 2.
// Renders one FeedDomain as a Bloomberg-terminal-style narrative feed:
//
//   ┌─ DOMAIN LABEL ─── [TIER] · N articles · HH:MM:SS UTC ──── [-] ───────┐
//   │████████████████ (2-px severity stripe on left)                        │
//   │ Reuters        │ 14:32 UTC   · Germany   [Rate +25bps] [CPI]          │
//   │ ECB Raises Rates 25bps — Core Inflation Remains Sticky at 3.6%        │
//   │ The European Central Bank lifted its deposit-facility rate …           │
//   ├───────────────────────────────────────────────────────────────────────┤
//   │ FT             │ 13:18 UTC   · Global    [NFP +250K]                   │
//   │ US Labour Market Surprises — Fed Reassesses Easing Path                │
//   │ Non-farm payrolls exceeded consensus by 40 000 …                       │
//   └───────────────────────────────────────────────────────────────────────┘
//
// Visual spec (§2 palette):
//   • Header bar: BG_SECONDARY, left accent stripe in domain colour
//   • Article card background: BG_PRIMARY / BG_ELEVATED on hover
//   • Left severity stripe: 3 px, severity_color(rec.severity)
//   • Source: ACCENT_CYAN_DIM   Timestamp: TEXT_MUTED
//   • Headline: TEXT_PRIMARY (word-wrapped, full text)
//   • Snippet:  TEXT_SECONDARY (≤280 chars collapsed, full on hover)
//   • Metric pills: semi-transparent severity-coloured rounded rects
// ─────────────────────────────────────────────────────────────────────────────
#include "ArticleRecord.hpp"
#include "../ui_common/Theme.hpp"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace macro {

class FeedModule {
public:
    static constexpr std::size_t MAX_ARTICLES = 50;

    explicit FeedModule(FeedDomain d) : domain_(d) {}

    // ── Data management (UI thread) ───────────────────────────────────────

    void replace_articles(std::vector<ArticleRecord> arts) {
        std::ranges::sort(arts, [](const ArticleRecord& a, const ArticleRecord& b){
            return a.published_at > b.published_at;
        });
        if (arts.size() > MAX_ARTICLES) arts.resize(MAX_ARTICLES);
        articles_     = std::deque<ArticleRecord>(arts.begin(), arts.end());
        is_loading_   = false;
        last_refresh_ = std::chrono::system_clock::now();
        error_msg_.clear();
    }

    void prepend_article(ArticleRecord art) {
        articles_.push_front(std::move(art));
        if (articles_.size() > MAX_ARTICLES) articles_.pop_back();
        last_refresh_ = std::chrono::system_clock::now();
    }

    void set_loading(bool v, std::string tier = "") {
        is_loading_ = v;
        if (!tier.empty()) tier_label_ = std::move(tier);
    }

    void set_error  (const std::string& m) { is_loading_ = false; error_msg_ = m; }
    void clear_error()                      { error_msg_.clear(); }

    [[nodiscard]] int article_count() const noexcept {
        return static_cast<int>(articles_.size());
    }

    // ── Render ────────────────────────────────────────────────────────────
    void render(float width) {
        render_header(width);
        if (collapsed_) return;

        if (is_loading_) { render_loading(width); return; }
        if (!error_msg_.empty()) { render_error(); return; }
        if (articles_.empty())   { render_empty(); return; }

        for (int i = 0; i < static_cast<int>(articles_.size()); ++i)
            render_card(articles_[static_cast<std::size_t>(i)], width, i);

        // Provenance footer
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - last_refresh_).count();
        ImGui::TextColored(age > 300 ? Theme::TEXT_STALE : Theme::TEXT_MUTED,
            "  %d articles · refreshed %llds ago · %s",
            article_count(), (long long)age, tier_label_.c_str());
        ImGui::Dummy({0, 6.0f});
    }

private:
    FeedDomain                domain_;
    std::deque<ArticleRecord> articles_;
    bool                      is_loading_{false};
    bool                      collapsed_{false};
    std::string               tier_label_{"GLOBAL"};
    std::string               error_msg_;
    std::chrono::system_clock::time_point last_refresh_{
        std::chrono::system_clock::now()};
    int hovered_idx_{-1};

    // ── Header ────────────────────────────────────────────────────────────
    void render_header(float width) {
        ImDrawList* dl  = ImGui::GetWindowDrawList();
        ImVec2      p   = ImGui::GetCursorScreenPos();
        const float hh  = 22.0f;
        ImVec4      acc = accent();

        // Background
        dl->AddRectFilled({p.x, p.y}, {p.x + width, p.y + hh},
            IM_COL32(13, 17, 23, 255));
        // 3-px left accent stripe
        dl->AddRectFilled({p.x, p.y}, {p.x + 3.0f, p.y + hh},
            ImGui::ColorConvertFloat4ToU32(acc));

        // Label
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
        ImGui::TextColored(acc, "%s", feed_domain_label(domain_));

        // Right: tier + count + spinner/live + collapse
        float rx = width - 170.0f;
        ImGui::SameLine(rx);
        if (!tier_label_.empty())
            ImGui::TextColored(Theme::ACCENT_CYAN_DIM, "[%s]", tier_label_.c_str());
        ImGui::SameLine(0, 10);
        if (is_loading_) {
            // Animated spinner
            static int tk = 0;
            static const char* SP[] = {"◐","◓","◑","◒"};
            ImGui::TextColored(Theme::SEV_ELEVATED, "%s", SP[(tk++ / 6) % 4]);
        } else {
            ImGui::TextColored(Theme::TEXT_MUTED, "%d", article_count());
        }
        ImGui::SameLine(0, 10);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::BG_ELEVATED);
        if (ImGui::SmallButton(collapsed_ ? "[+]" : "[-]")) collapsed_ = !collapsed_;
        ImGui::PopStyleColor(2);

        // Separator line
        ImGui::Dummy({0, 0});
        p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            {p.x, p.y}, {p.x + width, p.y}, IM_COL32(42,50,64,200), 1.0f);
    }

    // ── Article card ──────────────────────────────────────────────────────
    void render_card(const ArticleRecord& art, float width, int idx) {
        bool hovered = (hovered_idx_ == idx);
        float ch = card_h(art, hovered);

        ImDrawList* dl  = ImGui::GetWindowDrawList();
        ImVec2      p   = ImGui::GetCursorScreenPos();

        // Card background
        dl->AddRectFilled({p.x, p.y}, {p.x + width, p.y + ch},
            hovered ? ImGui::ColorConvertFloat4ToU32(Theme::BG_ELEVATED)
                    : IM_COL32(10, 14, 20, 220));

        // Severity stripe (3 px)
        ImVec4 sc = Theme::severity_color(art.severity);
        dl->AddRectFilled({p.x, p.y}, {p.x + 3.0f, p.y + ch},
            ImGui::ColorConvertFloat4ToU32(sc));

        float indent = 10.0f;

        // ── Row 1: Source · Timestamp · Geo · Metric pills ────────────────
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);

        ImGui::TextColored(Theme::ACCENT_CYAN_DIM, "%-13.13s", art.source_name.c_str());
        ImGui::SameLine(0, 8);
        ImGui::TextColored(Theme::TEXT_MUTED, "%s", fmt_ts(art.published_at).c_str());
        ImGui::SameLine(0, 8);
        ImGui::TextColored(Theme::TEXT_MUTED, "·");
        ImGui::SameLine(0, 8);
        ImGui::TextColored(Theme::TEXT_MUTED, "%s", art.geo_label.c_str());

        // Metric pills
        for (const auto& tag : art.metric_tags) {
            ImGui::SameLine(0, 10);
            render_pill(tag);
        }

        // ── Row 2: Headline ───────────────────────────────────────────────
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        float wrap_w = width - indent * 2.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
        ImGui::TextColored(Theme::TEXT_PRIMARY, "%s", art.headline.c_str());
        ImGui::PopTextWrapPos();

        // ── Row 3: Snippet ────────────────────────────────────────────────
        if (!art.snippet.empty()) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
            std::string snip = art.snippet;
            if (!hovered && snip.size() > 260) snip = snip.substr(0, 260) + "…";
            ImGui::TextColored(Theme::TEXT_SECONDARY, "%s", snip.c_str());
            ImGui::PopTextWrapPos();
        }

        // ── Row 4: URL chip (hover only) ──────────────────────────────────
        if (hovered && !art.source_url.empty()) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            std::string short_url = art.source_url.substr(
                0, std::min(art.source_url.size(), std::size_t{80}));
            ImGui::TextColored(Theme::ACCENT_CYAN_DIM, "↗  %s", short_url.c_str());
        }

        ImGui::Dummy({0, 5.0f});

        // Bottom divider
        p = ImGui::GetCursorScreenPos();
        dl->AddLine({p.x, p.y - 1.0f}, {p.x + width, p.y - 1.0f},
            IM_COL32(42, 50, 64, 100), 1.0f);

        // Hover detection
        ImVec2 card_min = {p.x, p.y - ch - 5.0f};
        ImVec2 card_max = {p.x + width, p.y};
        if (ImGui::IsMouseHoveringRect(card_min, card_max))
            hovered_idx_ = idx;
        else if (hovered_idx_ == idx)
            hovered_idx_ = -1;
    }

    void render_pill(const MetricTag& tag) {
        std::string txt = tag.label + " " + tag.value;
        ImVec2 sz = ImGui::CalcTextSize(txt.c_str());
        ImVec4 c  = Theme::severity_color(tag.severity);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float pw = sz.x + 10.0f, ph = sz.y + 4.0f;
        dl->AddRectFilled({p.x-2, p.y-1}, {p.x+pw, p.y+ph-1},
            ImGui::ColorConvertFloat4ToU32({c.x,c.y,c.z,0.18f}),
            Theme::ROUNDING_CHIP);
        dl->AddRect({p.x-2,p.y-1},{p.x+pw,p.y+ph-1},
            ImGui::ColorConvertFloat4ToU32({c.x,c.y,c.z,0.40f}),
            Theme::ROUNDING_CHIP, 0, 0.6f);
        ImGui::TextColored(c, "%s", txt.c_str());
        ImGui::Dummy({pw - sz.x + 2.0f, 0});
    }

    // ── Loading / error / empty ───────────────────────────────────────────
    void render_loading(float width) {
        ImGui::Dummy({0, 8.0f});
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
        ImGui::TextColored(Theme::ACCENT_CYAN_DIM,
            "  Fetching %s data for this viewport…", tier_label_.c_str());
        ImGui::Dummy({width, 24.0f});
    }

    void render_error() {
        ImGui::Dummy({0,4.0f});
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+12.0f);
        ImGui::TextColored(Theme::SEV_CRITICAL, "  ✕  %s", error_msg_.c_str());
        ImGui::Dummy({0,8.0f});
    }

    void render_empty() {
        ImGui::Dummy({0,4.0f});
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+12.0f);
        ImGui::TextColored(Theme::TEXT_MUTED,
            "  No %s data for current viewport.", tier_label_.c_str());
        ImGui::Dummy({0,8.0f});
    }

    // ── Helpers ───────────────────────────────────────────────────────────
    static float card_h(const ArticleRecord& art, bool hovered) noexcept {
        float h = 52.0f;
        if (!art.snippet.empty()) h += hovered ? 80.0f : 38.0f;
        if (hovered && !art.source_url.empty()) h += 20.0f;
        return h;
    }

    static std::string fmt_ts(const std::chrono::system_clock::time_point& tp) {
        auto tt = std::chrono::system_clock::to_time_t(tp);
        std::tm gm{};
#ifdef _WIN32
        gmtime_s(&gm, &tt);
#else
        gmtime_r(&tt, &gm);
#endif
        char buf[10];
        std::strftime(buf, sizeof(buf), "%H:%M UTC", &gm);
        return buf;
    }

    [[nodiscard]] ImVec4 accent() const noexcept {
        switch (domain_) {
            case FeedDomain::MacroDevelopments:    return Theme::SEV_ELEVATED;
            case FeedDomain::MicroDevelopments:    return Theme::DIR_BULLISH;
            case FeedDomain::GeopoliticalTensions: return Theme::SEV_HIGH;
            case FeedDomain::CentralBankUpdates:   return Theme::ACCENT_CYAN_DIM;
            case FeedDomain::MonetaryPolicy:       return Theme::SEV_ELEVATED;
            case FeedDomain::GlobalRegionalNews:   return Theme::TEXT_SECONDARY;
            case FeedDomain::MilitaryWarNews:      return Theme::SEV_CRITICAL;
        }
        return Theme::TEXT_MUTED;
    }
};

} // namespace macro
