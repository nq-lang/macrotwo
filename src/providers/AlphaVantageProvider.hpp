#pragma once
// src/providers/AlphaVantageProvider.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Alpha Vantage provider.
// Feeds: Table 6.2.2 (Central Bank / policy rate data),
//        Table 6.2.7 (Econ Calendar via REAL_GDP, INFLATION, etc.)
//        Layer 3 factor model (sector ETF price series)
// API docs: https://www.alphavantage.co/documentation/
// ─────────────────────────────────────────────────────────────────────────────
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>

namespace macro {

class AlphaVantageProvider final : public IDataProvider {
public:
    explicit AlphaVantageProvider(std::string api_key)
        : api_key_(std::move(api_key)) {}

    std::expected<void, ProviderError> connect() override {
        auto resp = http_.get(build_url("GLOBAL_QUOTE", "SPY"));
        if (!resp) return std::unexpected(resp.error());
        std::println("[AlphaVantage] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // ── Key macro economic functions ───────────────────────────────────
        static constexpr const char* MACRO_FNS[] = {
            "REAL_GDP", "INFLATION", "UNEMPLOYMENT",
            "FEDERAL_FUNDS_RATE", "TREASURY_YIELD",
            "CPI", "RETAIL_SALES", "DURABLES"
        };

        for (const char* fn : MACRO_FNS) {
            std::string url =
                std::format("https://www.alphavantage.co/query?function={}&apikey={}",
                            fn, api_key_);
            auto resp = http_.get(url);
            if (!resp) continue;

            try {
                auto j = nlohmann::json::parse(resp->body);
                // AV wraps macro data in "data" or "value" arrays
                std::string val_str;
                std::string date_str;

                if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
                    auto& latest = j["data"][0];
                    val_str  = latest.value("value", ".");
                    date_str = latest.value("date", "");
                }

                NormalizedRecord rec;
                rec.record_id    = std::format("alphavantage:{}", fn);
                rec.domain       = "econ_calendar";
                rec.source_name  = "Alpha Vantage";
                rec.headline     = std::format("{}: {} ({})", fn, val_str, date_str);
                rec.payload_json = resp->body.substr(0, 2048); // truncate large payloads
                rec.geo.country_iso2 = "US";
                rec.severity     = 1;
                rec.timestamp    = std::chrono::system_clock::now();
                co_yield std::move(rec);

            } catch (...) {}
        }

        // ── GICS Sector ETF snapshots for Layer 3 factor model ─────────────
        // XLK, XLF, XLE, XLU, XLV, XLP, XLI, XLB, XLY, XLRE, XLC
        static constexpr const char* SECTOR_ETFS[] = {
            "XLK","XLF","XLE","XLU","XLV","XLP","XLI","XLB","XLY","XLRE","XLC"
        };
        for (const char* etf : SECTOR_ETFS) {
            auto resp = http_.get(build_url("GLOBAL_QUOTE", etf));
            if (!resp) continue;
            try {
                auto j = nlohmann::json::parse(resp->body);
                NormalizedRecord rec;
                rec.record_id    = std::format("alphavantage:quote:{}", etf);
                rec.domain       = "sector_data";
                rec.source_name  = "Alpha Vantage";
                rec.headline     = std::format("{} snapshot", etf);
                rec.payload_json = j.dump();
                rec.geo.country_iso2 = "US";
                rec.timestamp    = std::chrono::system_clock::now();
                co_yield std::move(rec);
            } catch (...) {}
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Alpha Vantage";
    }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{300};
    }

private:
    std::string api_key_;
    HttpClient  http_;

    std::string build_url(const char* fn, const char* symbol) const {
        return std::format(
            "https://www.alphavantage.co/query?function={}&symbol={}&apikey={}",
            fn, symbol, api_key_);
    }
};

} // namespace macro
