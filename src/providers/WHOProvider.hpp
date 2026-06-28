#pragma once
// src/providers/WHOProvider.hpp
// WHO Global Health Observatory — free public REST API
// Feeds: Globe GMSI heat overlay (health crisis component),
//        Table 6.2.1 (geopolitical/systemic risk when health events flagged)
// API docs: https://www.who.int/data/gho/info/gho-odata-api
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>

namespace macro {

class WHOProvider final : public IDataProvider {
public:
    WHOProvider() = default;

    std::expected<void, ProviderError> connect() override {
        auto r = http_.get(
            "https://ghoapi.azureedge.net/api/Indicator?$top=1&$format=json");
        if (!r) return std::unexpected(r.error());
        std::println("[WHO] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // WHO indicators relevant to macro risk assessment
        struct WHOIndicator { const char* code; const char* label; int severity; };
        static constexpr WHOIndicator INDICATORS[] = {
            {"WHOSIS_000001", "Life expectancy at birth",               0},
            {"SA_0000001688", "Alcohol consumption per capita",         0},
            {"MDG_0000000026","Under-five mortality rate",               1},
            {"SDGPM25",       "PM2.5 air pollution (urban)",            1},
            // Disease burden — feeds systemic/crisis detection
            {"NCD_BMI_30A",   "Obesity prevalence (%)",                 0},
        };

        for (const auto& ind : INDICATORS) {
            std::string url = std::format(
                "https://ghoapi.azureedge.net/api/{}?$top=5&$format=json"
                "&$filter=SpatialDim%20ne%20null&$orderby=TimeDim%20desc",
                ind.code);

            auto r = http_.get(url);
            if (!r) continue;

            try {
                auto j = nlohmann::json::parse(r->body);
                if (!j.contains("value") || j["value"].empty()) continue;

                for (auto& row : j["value"]) {
                    std::string country = row.value("SpatialDim", "");
                    if (country.size() != 3) continue; // skip non-ISO3 entries
                    std::string year    = std::to_string(row.value("TimeDim", 0));
                    float       val     = row.value("NumericValue", 0.0f);

                    NormalizedRecord rec;
                    rec.record_id    = std::format("who:{}.{}.{}", ind.code, country, year);
                    rec.domain       = "geopolitics";
                    rec.source_name  = "WHO GHO";
                    rec.headline     = std::format("[WHO] {} {} {}: {:.2f}",
                                                   country, ind.label, year, val);
                    rec.payload_json = row.dump();
                    rec.geo.country_iso3 = country;
                    rec.severity     = ind.severity;
                    rec.timestamp    = std::chrono::system_clock::now();
                    co_yield std::move(rec);
                }

            } catch (const std::exception& e) {
                std::println("[WHO] parse error {}: {}", ind.code, e.what());
            }
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "WHO GHO"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{43200}; // 12 hours — annual/semi-annual data
    }

private:
    HttpClient http_;
};

} // namespace macro
