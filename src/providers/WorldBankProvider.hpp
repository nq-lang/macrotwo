#pragma once
// src/providers/WorldBankProvider.hpp
// World Bank Open Data — free public REST API (no key required)
// Feeds: Globe GMSI heat overlay, Table 6.2.1 (geopolitical context),
//        6.2.7 (economic calendar), right context panel country stats
// API docs: https://datahelpdesk.worldbank.org/knowledgebase/articles/898590
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>

namespace macro {

class WorldBankProvider final : public IDataProvider {
public:
    WorldBankProvider() = default;

    std::expected<void, ProviderError> connect() override {
        auto r = http_.get(
            "https://api.worldbank.org/v2/country/US/indicator/NY.GDP.MKTP.CD"
            "?format=json&mrv=1");
        if (!r) return std::unexpected(r.error());
        std::println("[WorldBank] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // World Bank indicator codes
        struct Indicator { const char* code; const char* label; const char* domain; };
        static constexpr Indicator INDICATORS[] = {
            {"NY.GDP.MKTP.CD",      "GDP (current USD)",        "econ_calendar"},
            {"NY.GDP.MKTP.KD.ZG",   "GDP growth (%)",           "econ_calendar"},
            {"FP.CPI.TOTL.ZG",      "CPI Inflation (%)",        "econ_calendar"},
            {"GC.DOD.TOTL.GD.ZS",   "Govt debt (% GDP)",        "monetary_policy"},
            {"BN.CAB.XOKA.GD.ZS",   "Current account (% GDP)",  "monetary_policy"},
            {"SL.UEM.TOTL.ZS",      "Unemployment rate (%)",    "econ_calendar"},
            {"IC.BUS.EASE.XQ",      "Ease of Doing Business",   "geopolitics"},
            {"PV.EST",              "Political Stability (WGI)", "geopolitics"},
        };

        // Key countries for GMSI computation
        static constexpr const char* COUNTRIES[] = {
            "US","GB","DE","JP","CN","FR","IN","BR","CA","AU",
            "KR","MX","ZA","TR","SA","NG","EG","AR","ID","RU"
        };

        for (const auto& ind : INDICATORS) {
            for (const char* iso2 : COUNTRIES) {
                std::string url = std::format(
                    "https://api.worldbank.org/v2/country/{}/indicator/{}"
                    "?format=json&mrv=2&per_page=2",
                    iso2, ind.code);

                auto r = http_.get(url);
                if (!r) continue;

                try {
                    auto j = nlohmann::json::parse(r->body);
                    if (!j.is_array() || j.size() < 2) continue;
                    auto& data = j[1];
                    if (!data.is_array() || data.empty()) continue;

                    auto& row = data[0];
                    if (row.is_null() || row.value("value", nullptr).is_null()) continue;

                    float val = 0.0f;
                    if (row["value"].is_number()) val = row["value"].get<float>();

                    NormalizedRecord rec;
                    rec.record_id    = std::format("wb:{}.{}", iso2, ind.code);
                    rec.domain       = ind.domain;
                    rec.source_name  = "World Bank";
                    rec.headline     = std::format("[WB] {} {}: {:.2f} ({})",
                        iso2, ind.label, val, row.value("date", ""));
                    rec.payload_json = row.dump();
                    rec.geo.country_iso2 = iso2;
                    rec.severity     = 1;
                    rec.timestamp    = std::chrono::system_clock::now();
                    co_yield std::move(rec);

                } catch (...) {}
            }
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "World Bank"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{7200}; // daily/annual data
    }

private:
    HttpClient http_;
};

} // namespace macro
