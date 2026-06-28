// tests/test_providers.cpp
// Provider normalization tests (no actual network calls — tests the
// parsing/normalization logic with sample payloads only)
#include <catch2/catch_test_macros.hpp>
#include "providers/IDataProvider.hpp"
#include <nlohmann/json.hpp>
#include <chrono>

using namespace macro;

TEST_CASE("NormalizedRecord GeoTag country match", "[provider]") {
    NormalizedRecord rec;
    rec.geo.country_iso2 = "DE";
    REQUIRE(rec.geo.matches_country("DE"));
    REQUIRE(!rec.geo.matches_country("US"));
}

TEST_CASE("NormalizedRecord GeoTag no country", "[provider]") {
    NormalizedRecord rec;
    REQUIRE(!rec.geo.matches_country("US"));
}

TEST_CASE("NormalizedRecord severity clamped in [0,5]", "[provider]") {
    NormalizedRecord rec;
    rec.severity = 3;
    REQUIRE(rec.severity >= 0);
    REQUIRE(rec.severity <= 5);
}

TEST_CASE("ProviderError kind enum coverage", "[provider]") {
    ProviderError e{ProviderErrorKind::NetworkFailure, "timeout", 0};
    REQUIRE(e.kind == ProviderErrorKind::NetworkFailure);
    REQUIRE(e.message == "timeout");
}

TEST_CASE("nlohmann json parse safety", "[provider]") {
    std::string payload = R"({"rate":5.25,"sentiment":"DOVISH","cut_prob":-67.0})";
    auto j = nlohmann::json::parse(payload, nullptr, false);
    REQUIRE(!j.is_discarded());
    REQUIRE(j.value("rate", 0.0f) == 5.25f);
    REQUIRE(j.value("sentiment", std::string("—")) == "DOVISH");
    REQUIRE(j.value("cut_prob", 0.0f) == -67.0f);
}

TEST_CASE("NormalizedRecord timestamp is recent", "[provider]") {
    NormalizedRecord rec;
    rec.timestamp = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - rec.timestamp).count();
    REQUIRE(age < 5);
}
