#pragma once
// src/tables/ArticleRecord.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Rich article/headline record — the display unit for Section 2.
// Replaces the legacy NormalizedRecord grid-row model with a qualitative,
// narrative-first data structure suitable for Bloomberg-terminal-style feeds.
// ─────────────────────────────────────────────────────────────────────────────
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace macro {

/// Section 2 feed domain identifiers — the 7 vertical modules.
enum class FeedDomain : int {
    MacroDevelopments    = 0, ///< GDP, employment, inflation narratives
    MicroDevelopments    = 1, ///< Corporate earnings, sector news
    GeopoliticalTensions = 2, ///< Diplomatic, trade wars, treaties
    CentralBankUpdates   = 3, ///< CB speeches, minutes, balance sheet
    MonetaryPolicy       = 4, ///< Rate decisions, forward guidance, QE/QT
    GlobalRegionalNews   = 5, ///< Broad economic, supply chain, local inflation
    MilitaryWarNews      = 6, ///< Conflicts, defense spending, security
};

static constexpr int FEED_DOMAIN_COUNT = 7;

/// String label for each domain — rendered as section headers.
[[nodiscard]] inline const char* feed_domain_label(FeedDomain d) noexcept {
    switch (d) {
        case FeedDomain::MacroDevelopments:    return "MACROECONOMIC DEVELOPMENTS";
        case FeedDomain::MicroDevelopments:    return "MICROECONOMIC DEVELOPMENTS";
        case FeedDomain::GeopoliticalTensions: return "GEOPOLITICAL & GEO-TENSIONS";
        case FeedDomain::CentralBankUpdates:   return "CENTRAL BANK UPDATES";
        case FeedDomain::MonetaryPolicy:       return "MONETARY POLICY";
        case FeedDomain::GlobalRegionalNews:   return "GLOBAL / REGIONAL NEWS";
        case FeedDomain::MilitaryWarNews:      return "MILITARY & WAR NEWS";
    }
    return "UNKNOWN";
}

/// Contextual metric tag embedded inline within an article card.
/// e.g. { "CPI", "+3.2% YoY", severity=2 }
struct MetricTag {
    std::string label;   ///< "CPI", "Rate", "GDP", "P/C Ratio" …
    std::string value;   ///< "+3.2% YoY", "5.25%", "-0.4%" …
    int         severity{1};
};

/// An article/headline record as displayed in Section 2 feeds.
/// Constructed either from ingested NormalizedRecords or from
/// on-demand GeoScopedFetcher API pulls.
struct ArticleRecord {
    std::string id;              ///< Dedup key (url hash or provider:id)
    FeedDomain  domain;
    std::string source_name;     ///< "Reuters", "FT", "FRED", "ECB" …
    std::string source_url;      ///< Original article URL (optional)
    std::string headline;        ///< Full headline (≤160 chars)
    std::string snippet;         ///< 1–3 sentence summary / lede paragraph
    std::vector<MetricTag> metric_tags; ///< Embedded quantitative tags
    int         severity{0};     ///< 0–5 severity level
    std::chrono::system_clock::time_point published_at;

    // Geographic provenance
    std::string geo_label;       ///< "Global", "Europe", "Germany", "Bavaria"

    // Fetch provenance
    std::string fetch_tier_label; ///< "GLOBAL", "CONTINENT", "COUNTRY", "LOCAL"

    /// True if this record was fetched live for the current viewport
    /// (as opposed to ingested from the background provider pipeline).
    bool is_geo_fetched{false};
};

} // namespace macro
