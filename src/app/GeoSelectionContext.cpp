// src/app/GeoSelectionContext.cpp
#include "GeoSelectionContext.hpp"
#include <format>
#include <algorithm>
#include <cctype>

namespace macro {

std::string GeoSelectionContext::breadcrumb() const {
    std::string bc = "World";
    if (resolution >= GeoResolution::Continent && !continent.empty())
        bc += std::format(" / {}", continent);
    if (resolution >= GeoResolution::Country && !country_name.empty())
        bc += std::format(" / {}", country_name);
    if (resolution >= GeoResolution::State && admin1_name)
        bc += std::format(" / {}", *admin1_name);
    if (resolution >= GeoResolution::City && city_name)
        bc += std::format(" / {}", *city_name);
    if (resolution == GeoResolution::Ground)
        bc += " / Ground Intelligence";
    return bc;
}

std::string GeoSelectionContext::selected_name() const {
    switch (resolution) {
        case GeoResolution::World:     return "World";
        case GeoResolution::Continent: return continent;
        case GeoResolution::Country:   return country_name;
        case GeoResolution::State:     return admin1_name.value_or(country_name);
        case GeoResolution::City:      return city_name.value_or(country_name);
        case GeoResolution::Ground:    return "Ground Intelligence";
    }
    return "Unknown";
}

std::string GeoSelectionContext::to_query_string() const {
    // Builds a search query scoped to the current viewport tier.
    // Used verbatim as the `q=` parameter for NewsAPI, GNews, etc.
    switch (resolution) {
        case GeoResolution::World:
            return "global economy OR macroeconomics OR central bank OR geopolitics "
                   "OR inflation OR GDP OR interest rates OR trade";

        case GeoResolution::Continent: {
            std::string q = continent + " economy OR " + continent + " markets OR "
                          + continent + " central bank OR " + continent + " inflation";
            return q;
        }

        case GeoResolution::Country:
            if (!country_name.empty())
                return country_name + " economy OR " + country_name + " inflation OR "
                     + country_name + " interest rates OR " + country_name
                     + " central bank OR " + country_name + " GDP OR "
                     + country_iso2 + " financial news";
            return "global economy";

        case GeoResolution::State:
        case GeoResolution::City: {
            std::string loc = city_name ? *city_name : admin1_name.value_or(country_name);
            return loc + " economy OR " + loc + " business OR "
                 + country_name + " regional economy OR " + loc + " financial";
        }

        case GeoResolution::Ground:
            return ""; // suppressed at satellite tier
    }
    return "global economy";
}

std::string GeoSelectionContext::to_api_geo_param() const {
    // Returns a NewsAPI-style country filter string
    switch (resolution) {
        case GeoResolution::World:     return "";
        case GeoResolution::Continent: return "";
        case GeoResolution::Country:
            if (!country_iso2.empty()) {
                // NewsAPI uses lowercase 2-letter country codes
                std::string lc = country_iso2;
                std::ranges::transform(lc, lc.begin(), ::tolower);
                return "&country=" + lc;
            }
            return "";
        case GeoResolution::State:
        case GeoResolution::City:
        case GeoResolution::Ground:
            return "";
    }
    return "";
}

} // namespace macro
