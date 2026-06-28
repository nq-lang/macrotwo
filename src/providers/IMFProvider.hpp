#pragma once
// src/providers/IMFProvider.hpp
// IMF International Monetary Fund — free public REST API
// No API key required. Uses IMF Data API v2.
// Feeds: Table 6.2.2 (CB policy context), 6.2.3 (monetary policy),
//        6.2.7 (economic calendar), globe heat overlay (GMSI inputs)
// API docs: https://datahelp.imf.org/knowledgebase/articles/667681
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>

namespace macro {

class IMFProvider final : public IDataProvider {
public:
    IMFProvider() = default;

    std::expected<void, ProviderError> connect() override {
        // Test against IFS (International Financial Statistics) dataflow
        auto r = http_.get(
            "https://dataservices.imf.org/REST/SDMX_JSON.svc/Dataflow");
        if (!r) return std::unexpected(r.error());
        std::println("[IMF] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // ── WEO GDP growth projections by country ─────────────────────────
        // NGDP_RPCH = Real GDP growth rate (annual percent change)
        // Key economies: US, DE, JP, CN, GB, FR, IN, BR, RU, CA
        struct SeriesSpec { const char* db; const char* indicator; const char* area; };
        static constexpr SeriesSpec SERIES[] = {
            {"WEO", "NGDP_RPCH", "US"},
            {"WEO", "NGDP_RPCH", "DE"},
            {"WEO", "NGDP_RPCH", "JP"},
            {"WEO", "NGDP_RPCH", "GB"},
            {"WEO", "NGDP_RPCH", "CN"},
            {"WEO", "PCPIPCH",   "US"},  // CPI inflation
            {"WEO", "PCPIPCH",   "DE"},
            {"WEO", "PCPIPCH",   "GB"},
            // IFS — current account balance
            {"IFS", "BCA_BP6_USD", "US"},
            {"IFS", "BCA_BP6_USD", "DE"},
            {"IFS", "BCA_BP6_USD", "JP"},
        };

        for (const auto& s : SERIES) {
            // CompactData endpoint: /CompactData/{db}/{freq}.{indicator}.{area}
            std::string url = std::format(
                "https://dataservices.imf.org/REST/SDMX_JSON.svc/CompactData"
                "/{}/A.{}.{}?startPeriod=2020&endPeriod=2025",
                s.db, s.indicator, s.area);

            auto r = http_.get(url);
            if (!r) {
                std::println("[IMF] failed {}/{}/{}: {}", s.db, s.indicator, s.area,
                             r.error().message);
                continue;
            }

            try {
                auto j = nlohmann::json::parse(r->body);
                // Navigate SDMX-JSON structure
                auto& ds = j["CompactData"]["DataSet"];
                if (!ds.contains("Series")) continue;
                auto& series = ds["Series"];
                auto& obs    = series["Obs"];
                if (!obs.is_array() || obs.empty()) continue;

                // Take the most recent observation
                auto& latest  = obs.back();
                std::string period = latest.value("@TIME_PERIOD", "");
                std::string value  = latest.value("@OBS_VALUE",   "—");

                NormalizedRecord rec;
                rec.record_id    = std::format("imf:{}.{}.{}", s.db, s.indicator, s.area);
                rec.domain       = std::string(s.db) == "WEO" ? "econ_calendar" : "monetary_policy";
                rec.source_name  = "IMF";
                rec.headline     = std::format("[IMF] {}/{} {}: {} ({})",
                                               s.area, s.indicator, s.db, value, period);
                rec.payload_json = r->body.substr(0, 1024);
                rec.geo.country_iso2 = s.area;
                rec.severity     = 1;
                rec.timestamp    = std::chrono::system_clock::now();
                co_yield std::move(rec);

            } catch (const std::exception& e) {
                std::println("[IMF] parse error {}: {}", s.indicator, e.what());
            }
        }

        // ── SDR (Special Drawing Rights) rate — key CB reserve indicator ──
        {
            auto r = http_.get(
                "https://dataservices.imf.org/REST/SDMX_JSON.svc/CompactData"
                "/IFS/M.US.ENDA_XDC_USD_RATE?startPeriod=2024-01&endPeriod=2025-12");
            if (r) {
                NormalizedRecord rec;
                rec.record_id    = "imf:sdr:rate";
                rec.domain       = "central_bank";
                rec.source_name  = "IMF";
                rec.headline     = "IMF SDR/USD Rate (latest)";
                rec.payload_json = r->body.substr(0, 1024);
                rec.geo.country_iso2 = "US";
                rec.severity     = 1;
                rec.timestamp    = std::chrono::system_clock::now();
                co_yield std::move(rec);
            }
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "IMF"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{3600}; // IMF data is updated daily/monthly
    }

private:
    HttpClient http_;
};

} // namespace macro
