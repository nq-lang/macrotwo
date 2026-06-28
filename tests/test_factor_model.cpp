// tests/test_factor_model.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "topography/FactorModel.hpp"

using namespace macro;
using Catch::Approx;

TEST_CASE("TerrainGrid C++23 mdspan multidim operator[]", "[terrain]") {
    TerrainGrid g(4, 8);
    g[0, 0] = 1.5f;
    g[3, 7] = -2.0f;
    REQUIRE(g[0, 0] == Approx(1.5f));
    REQUIRE(g[3, 7] == Approx(-2.0f));
}

TEST_CASE("TerrainGrid dimensions", "[terrain]") {
    TerrainGrid g(N_SECTORS, 32);
    REQUIRE(g.rows() == static_cast<std::size_t>(N_SECTORS));
    REQUIRE(g.cols() == 32u);
}

TEST_CASE("FactorModel stub recompute produces results", "[factor_model]") {
    FactorModel model;
    bool ok = model.recompute();
    // With no real data, should return false (stub) but populate results
    (void)ok;
    REQUIRE(model.results_us().size() == static_cast<std::size_t>(N_SECTORS));
    REQUIRE(model.results_eu().size() == static_cast<std::size_t>(N_SECTORS));
}

TEST_CASE("FactorModel regime score in [-3, 3]", "[factor_model]") {
    FactorModel model;
    model.recompute();
    for (const auto& r : model.results_us()) {
        REQUIRE(r.regime_score >= -3.1f);
        REQUIRE(r.regime_score <=  3.1f);
    }
}

TEST_CASE("FactorModel conviction in [0, 1]", "[factor_model]") {
    FactorModel model;
    model.recompute();
    for (const auto& r : model.results_us()) {
        REQUIRE(r.conviction >= 0.0f);
        REQUIRE(r.conviction <= 1.0f);
    }
}

TEST_CASE("FactorModel build_terrain produces correct grid dims", "[terrain]") {
    FactorModel model;
    model.recompute();
    auto grid = model.build_terrain(model.results_us(), 32);
    REQUIRE(grid.rows() == static_cast<std::size_t>(N_SECTORS));
    REQUIRE(grid.cols() == 32u);
}

TEST_CASE("GICS sector names not empty", "[factor_model]") {
    for (auto sv : GICS_SECTORS) {
        REQUIRE(!sv.empty());
    }
}
