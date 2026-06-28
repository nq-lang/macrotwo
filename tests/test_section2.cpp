// tests/test_section2.cpp
// Section 2 remodel — unit tests for debounce logic, domain routing,
// ArticleRecord construction, GeoScopedFetcher rate limiting, FeedModule state.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "tables/ArticleRecord.hpp"
#include "app/GeoSelectionContext.hpp"
#include <thread>
#include <chrono>

using namespace macro;

// ── ArticleRecord ────────────────────────────────────────────────────────────
TEST_CASE("ArticleRecord domain labels non-empty", "[article]") {
    for (int d = 0; d < FEED_DOMAIN_COUNT; ++d) {
        const char* lbl = feed_domain_label(static_cast<FeedDomain>(d));
        REQUIRE(lbl != nullptr);
        REQUIRE(std::strlen(lbl) > 0);
    }
}

TEST_CASE("ArticleRecord FEED_DOMAIN_COUNT == 7", "[article]") {
    REQUIRE(FEED_DOMAIN_COUNT == 7);
}

TEST_CASE("MetricTag stores label and value", "[article]") {
    MetricTag t; t.label = "CPI"; t.value = "+3.2%"; t.severity = 2;
    REQUIRE(t.label == "CPI");
    REQUIRE(t.value == "+3.2%");
    REQUIRE(t.severity == 2);
}

TEST_CASE("FeedDomain enum values sequential 0-6", "[article]") {
    REQUIRE(static_cast<int>(FeedDomain::MacroDevelopments)    == 0);
    REQUIRE(static_cast<int>(FeedDomain::MicroDevelopments)    == 1);
    REQUIRE(static_cast<int>(FeedDomain::GeopoliticalTensions) == 2);
    REQUIRE(static_cast<int>(FeedDomain::CentralBankUpdates)   == 3);
    REQUIRE(static_cast<int>(FeedDomain::MonetaryPolicy)       == 4);
    REQUIRE(static_cast<int>(FeedDomain::GlobalRegionalNews)   == 5);
    REQUIRE(static_cast<int>(FeedDomain::MilitaryWarNews)      == 6);
}

// ── GeoSelectionContext zoom tier ────────────────────────────────────────────
TEST_CASE("fetch_tier World == 0", "[geo_tier]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::World;
    REQUIRE(fetch_tier(ctx.resolution) == 0);
}

TEST_CASE("fetch_tier Continent == 1", "[geo_tier]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::Continent;
    REQUIRE(fetch_tier(ctx.resolution) == 1);
}

TEST_CASE("fetch_tier Country == 2", "[geo_tier]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::Country;
    REQUIRE(fetch_tier(ctx.resolution) == 2);
}

TEST_CASE("fetch_tier State == 3", "[geo_tier]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::State;
    REQUIRE(fetch_tier(ctx.resolution) == 3);
}

TEST_CASE("fetch_tier City == 3", "[geo_tier]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::City;
    REQUIRE(fetch_tier(ctx.resolution) == 3);
}

TEST_CASE("fetch_tier Ground == 4 (suppressed)", "[geo_tier]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::Ground;
    REQUIRE(fetch_tier(ctx.resolution) == 4);
}

// ── GeoSelectionContext query building ──────────────────────────────────────
TEST_CASE("to_query_string World returns non-empty", "[geo_query]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::World;
    REQUIRE(!ctx.to_query_string().empty());
}

TEST_CASE("to_query_string Country includes country name", "[geo_query]") {
    GeoSelectionContext ctx;
    ctx.resolution   = GeoResolution::Country;
    ctx.country_name = "Germany";
    ctx.country_iso2 = "DE";
    std::string q = ctx.to_query_string();
    REQUIRE(q.find("Germany") != std::string::npos);
}

TEST_CASE("to_query_string Ground returns empty (suppressed)", "[geo_query]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::Ground;
    REQUIRE(ctx.to_query_string().empty());
}

TEST_CASE("to_api_geo_param Country returns NewsAPI country param", "[geo_query]") {
    GeoSelectionContext ctx;
    ctx.resolution   = GeoResolution::Country;
    ctx.country_iso2 = "DE";
    std::string p = ctx.to_api_geo_param();
    REQUIRE(p.find("de") != std::string::npos);
}

TEST_CASE("to_api_geo_param World returns empty", "[geo_query]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::World;
    REQUIRE(ctx.to_api_geo_param().empty());
}

// ── Debounce logic (settled_at) ──────────────────────────────────────────────
TEST_CASE("GeoSelectionContext is_settled true after delay", "[debounce]") {
    GeoSelectionContext ctx;
    ctx.settled_at = std::chrono::steady_clock::now() - std::chrono::milliseconds{2100};
    REQUIRE(ctx.is_settled(2000));
}

TEST_CASE("GeoSelectionContext is_settled false immediately", "[debounce]") {
    GeoSelectionContext ctx;
    ctx.settled_at = std::chrono::steady_clock::now();
    REQUIRE(!ctx.is_settled(2000));
}

TEST_CASE("GeoSelectionContext is_settled with custom threshold", "[debounce]") {
    GeoSelectionContext ctx;
    ctx.settled_at = std::chrono::steady_clock::now() - std::chrono::milliseconds{600};
    REQUIRE( ctx.is_settled(500));
    REQUIRE(!ctx.is_settled(700));
}
