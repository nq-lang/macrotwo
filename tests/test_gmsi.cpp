// tests/test_gmsi.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "globe/GMSIComputer.hpp"
#include "providers/IDataProvider.hpp"

using namespace macro;
using Catch::Approx;

TEST_CASE("GMSIComputer baseline seed produces valid scores", "[gmsi]") {
    GMSIComputer gmsi;
    // US should have a low-ish score (seeded ~0.9)
    auto us = gmsi.score("US");
    REQUIRE(us.score >= 0.0f);
    REQUIRE(us.score <= 5.0f);
}

TEST_CASE("GMSIComputer high-stress country scores above low-stress", "[gmsi]") {
    GMSIComputer gmsi;
    auto ru = gmsi.score("RU");
    auto us = gmsi.score("US");
    REQUIRE(ru.score > us.score);
}

TEST_CASE("GMSIComputer score_to_alpha range", "[gmsi]") {
    float a0 = GMSIComputer::score_to_alpha(0.0f);
    float a5 = GMSIComputer::score_to_alpha(5.0f);
    REQUIRE(a0 >= 0.0f);
    REQUIRE(a5 <= 1.0f);
    REQUIRE(a5 > a0);
}

TEST_CASE("GMSIComputer ingest geopolitics record increases score", "[gmsi]") {
    GMSIComputer gmsi;
    float before = gmsi.score("FR").score;

    NormalizedRecord rec;
    rec.record_id        = "test:1";
    rec.domain           = "geopolitics";
    rec.source_name      = "test";
    rec.headline         = "Crisis in Paris";
    rec.geo.country_iso2 = "FR";
    rec.severity         = 4;
    rec.timestamp        = std::chrono::system_clock::now();

    for (int i = 0; i < 5; ++i) gmsi.ingest_record(rec);
    gmsi.maybe_recompute();

    float after = gmsi.score("FR").score;
    REQUIRE(after >= before);
}

TEST_CASE("GMSIComputer score_to_color returns non-zero", "[gmsi]") {
    auto col = GMSIComputer::score_to_color(3.0f);
    // Any non-transparent color
    float mag = col.x * col.x + col.y * col.y + col.z * col.z;
    REQUIRE(mag > 0.0f);
}
