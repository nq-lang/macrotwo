#pragma once
// src/providers/FredProvider.hpp
// ─────────────────────────────────────────────────────────────────────────────
// FRED (Federal Reserve Economic Data) provider.
// Feeds: Table 6.2.7 (Economic Calendar / Prints) and
//        Table 6.2.3 (Monetary Policy — policy-rate series).
// API docs: https://fred.stlouisfed.org/docs/api/fred/
// ─────────────────────────────────────────────────────────────────────────────
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>
#include <string>

namespace macro {

class FredProvider final : public IDataProvider {
public:
    explicit FredProvider(std::string api_key)
        : api_key_(std::move(api_key)) {}

    std::expected<void, ProviderError> connect() override {
        // Ping FRED with a lightweight series info call to validate the key.
        auto resp = http_.get(build_url("series", "FEDFUNDS"));
        if (!resp) {
            std::println("[FRED] connect failed: {}", resp.error().message);
            return std::unexpected(resp.error());
        }
        std::println("[FRED] connected OK (HTTP {})", resp->status_code);
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // ── Fetch key economic indicator releases ─────────────────────────
        static constexpr const char* KEY_SERIES[] = {
            "FEDFUNDS",   // Fed Funds Rate
            "DGS10",      // 10-Year Treasury
            "CPIAUCSL",   // CPI
            "UNRATE",     // Unemployment Rate
            "GDP",        // Real GDP
            "INDPRO",     // Industrial Production
            "HOUST",      // Housing Starts
        };

        for (const char* series_id : KEY_SERIES) {
            auto resp = http_.get(build_url("series/observations", series_id,
                                            "&sort_order=desc&limit=2"));
            if (!resp) {
                std::println("[FRED] poll error for {}: {}", series_id, resp.error().message);
                continue;
            }

            auto rec = parse_observation(series_id, resp->body);
            if (rec) co_yield std::move(*rec);
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return "FRED";
    }

    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{300}; // FRED data updates at most daily
    }

private:
    std::string  api_key_;
    HttpClient   http_;

    std::string build_url(std::string_view endpoint, std::string_view series_id,
                          std::string_view extra = "") const {
        return std::format(
            "https://api.stlouisfed.org/fred/{}?series_id={}&api_key={}&file_type=json{}",
            endpoint, series_id, api_key_, extra);
    }

    std::optional<NormalizedRecord> parse_observation(
        const char* series_id, const std::string& body) {
        try {
            auto j = nlohmann::json::parse(body);
            if (!j.contains("observations") || j["observations"].empty())
                return std::nullopt;

            auto& obs = j["observations"][0];
            std::string value = obs.value("value", ".");
            std::string date  = obs.value("date",  "");

            NormalizedRecord rec;
            rec.record_id    = std::format("fred:{}", series_id);
            rec.domain       = "econ_calendar";
            rec.source_name  = "FRED";
            rec.headline     = std::format("{}: {} ({})", series_id, value, date);
            rec.payload_json = body;
            rec.geo.country_iso2 = "US";
            rec.geo.continent    = "North America";
            rec.severity     = 1; // low — economic print
            rec.timestamp    = std::chrono::system_clock::now();
            return rec;

        } catch (const nlohmann::json::exception& e) {
            std::println("[FRED] parse error for {}: {}", series_id, e.what());
            return std::nullopt;
        }
    }
};

} // namespace macro
