#pragma once
// src/app/GeoSelectionContext.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Authoritative geographic + zoom state produced by Section 1 (Globe).
// Extended to carry zoom depth, bounding box, and debounce timestamp so
// Section 2 can implement the 2-second debounce on fetch triggers.
// ─────────────────────────────────────────────────────────────────────────────
#include <array>
#include <chrono>
#include <optional>
#include <string>

namespace macro {

/// Zoom depth enum — maps 1:1 onto the fetch-scope hierarchy.
enum class GeoResolution : int {
    World     = 0,   ///< Zoom 0 — full globe, global macro feeds
    Continent = 1,   ///< Zoom 1 — continent/bloc, regional feeds
    Country   = 2,   ///< Zoom 2 — sovereign, domestic feeds
    State     = 3,   ///< Zoom 3 — sub-national, local feeds
    City      = 4,   ///< Zoom 3 — city-level (same fetch tier as State)
    Ground    = 5,   ///< Zoom 4 — satellite / GEOINT pane (no feed refresh)
};

/// Returns the fetch tier (0–3) for a given resolution.
/// Tier 4 (Ground) suppresses all Section 2 fetches.
[[nodiscard]] inline int fetch_tier(GeoResolution r) noexcept {
    switch (r) {
        case GeoResolution::World:     return 0;
        case GeoResolution::Continent: return 1;
        case GeoResolution::Country:   return 2;
        case GeoResolution::State:
        case GeoResolution::City:      return 3;
        case GeoResolution::Ground:    return 4; // suppress
    }
    return 0;
}

/// Immutable snapshot of current geographic selection + zoom state.
/// Published by GlobeLayer on every drill-down / pan-settle event.
struct GeoSelectionContext {
    GeoResolution resolution{GeoResolution::World};

    // ── Labels ────────────────────────────────────────────────────────────
    std::string continent;
    std::string country_name;
    std::string country_iso2;
    std::string country_iso3;
    std::optional<std::string> admin1_name;
    std::optional<std::string> city_name;

    // ── Geospatial ────────────────────────────────────────────────────────
    double lat{0.0};
    double lon{0.0};
    /// WGS84 bounding box [minLon, minLat, maxLon, maxLat]
    std::array<double, 4> bbox{-180.0, -90.0, 180.0, 90.0};

    // ── Bloc memberships ──────────────────────────────────────────────────
    bool is_g7{false};
    bool is_g20{false};
    bool is_eurozone{false};
    bool is_eu{false};
    bool is_nato{false};
    bool is_em{false};

    // ── Debounce timestamp ────────────────────────────────────────────────
    /// Wall-clock instant when this context became stable (last change).
    /// Section 2 compares now() − settled_at against the 2-second threshold
    /// before issuing any network fetch.
    std::chrono::steady_clock::time_point settled_at{
        std::chrono::steady_clock::now()};

    // ── Helpers ───────────────────────────────────────────────────────────
    [[nodiscard]] std::string breadcrumb() const;
    [[nodiscard]] std::string selected_name() const;

    /// True when the context has been stable for at least `ms` milliseconds.
    [[nodiscard]] bool is_settled(int ms = 2000) const noexcept {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - settled_at).count();
        return age >= ms;
    }

    /// Build a concise search query string suitable for news APIs
    /// at the current zoom tier.
    [[nodiscard]] std::string to_query_string() const;

    /// Returns a locale string for API geo-filtering
    /// e.g. "country=DE" at Country tier, "q=Bavaria Germany" at State tier
    [[nodiscard]] std::string to_api_geo_param() const;
};

} // namespace macro
