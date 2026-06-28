// tests/test_geo_context.cpp
#include <catch2/catch_test_macros.hpp>
#include "app/GeoSelectionContext.hpp"
#include "app/AppStateBus.hpp"
using namespace macro;

TEST_CASE("breadcrumb World", "[geo]") {
    GeoSelectionContext ctx;
    REQUIRE(ctx.breadcrumb() == "World");
}
TEST_CASE("breadcrumb Continent", "[geo]") {
    GeoSelectionContext ctx;
    ctx.resolution = GeoResolution::Continent;
    ctx.continent  = "Europe";
    REQUIRE(ctx.breadcrumb() == "World / Europe");
}
TEST_CASE("breadcrumb Country", "[geo]") {
    GeoSelectionContext ctx;
    ctx.resolution   = GeoResolution::Country;
    ctx.continent    = "Europe";
    ctx.country_name = "Germany";
    REQUIRE(ctx.breadcrumb() == "World / Europe / Germany");
}
TEST_CASE("selected_name World", "[geo]") {
    GeoSelectionContext ctx;
    REQUIRE(ctx.selected_name() == "World");
}
TEST_CASE("selected_name Country", "[geo]") {
    GeoSelectionContext ctx;
    ctx.resolution   = GeoResolution::Country;
    ctx.country_name = "Japan";
    REQUIRE(ctx.selected_name() == "Japan");
}
TEST_CASE("AppStateBus subscribe dispatch", "[bus]") {
    AppStateBus bus;
    int n = 0;
    GeoSelectionContext got;
    auto tok = bus.subscribe([&](const GeoSelectionContext& c){ ++n; got=c; });
    GeoSelectionContext ctx; ctx.resolution=GeoResolution::Country; ctx.country_name="France";
    bus.publish(ctx);
    REQUIRE(bus.dispatch_pending() == 1u);
    REQUIRE(n == 1);
    REQUIRE(got.country_name == "France");
    bus.unsubscribe(tok);
}
TEST_CASE("AppStateBus unsubscribe stops callbacks", "[bus]") {
    AppStateBus bus; int n=0;
    auto t = bus.subscribe([&](const GeoSelectionContext&){++n;});
    bus.unsubscribe(t);
    bus.publish(GeoSelectionContext{}); bus.dispatch_pending();
    REQUIRE(n == 0);
}
TEST_CASE("AppStateBus multiple subscribers", "[bus]") {
    AppStateBus bus; int a=0,b=0;
    auto ta = bus.subscribe([&](const GeoSelectionContext&){++a;});
    auto tb = bus.subscribe([&](const GeoSelectionContext&){++b;});
    bus.publish(GeoSelectionContext{});
    bus.publish(GeoSelectionContext{});
    bus.dispatch_pending();
    REQUIRE(a==2); REQUIRE(b==2);
    bus.unsubscribe(ta); bus.unsubscribe(tb);
}
