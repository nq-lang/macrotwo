#pragma once
// src/providers/MarketStackProvider.hpp
// MarketStack API — real-time and historical global stock market data
// Feeds: Layer 3 sector returns, Table 6.2.4 positioning snapshots
// API docs: https://marketstack.com/documentation
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>

namespace macro {

class MarketStackProvider final : public IDataProvider {
public:
    explicit MarketStackProvider(std::string api_key)
        : api_key_(std::move(api_key)) {}

    std::expected<void, ProviderError> connect() override {
        auto r = http_.get(
            std::format("http://api.marketstack.com/v1/eod?access_key={}&symbols=AAPL&limit=1",
                        api_key_));
        if (!r) return std::unexpected(r.error());
        if (r->status_code != 200)
            return std::unexpected(ProviderError{ProviderErrorKind::AuthFailure,
                "HTTP " + std::to_string(r->status_code)});
        std::println("[MarketStack] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // Pull end-of-day data for GICS sector ETFs + major indices
        static constexpr const char* SYMBOLS =
            "SPY,QQQ,IWM,XLK,XLF,XLE,XLU,XLV,XLP,XLI,XLB,XLY,XLRE,XLC,"
            "EWJ,EWG,EWQ,EWU,FEZ,VGK";

        auto r = http_.get(
            std::format("http://api.marketstack.com/v1/eod?access_key={}"
                        "&symbols={}&limit=20&sort=DESC", api_key_, SYMBOLS));
        if (!r) { std::println("[MarketStack] poll error: {}", r.error().message); co_return; }

        try {
            auto j = nlohmann::json::parse(r->body);
            for (auto& item : j.value("data", nlohmann::json::array())) {
                NormalizedRecord rec;
                std::string sym = item.value("symbol", "");
                rec.record_id   = std::format("marketstack:eod:{}", sym);
                rec.domain      = "sector_data";
                rec.source_name = "MarketStack";
                rec.headline    = std::format("{} close={} vol={}",
                    sym,
                    item.value("close", 0.0f),
                    item.value("volume", 0LL));
                rec.payload_json = item.dump();
                rec.geo.country_iso2 = item.value("exchange", "").substr(0, 2);
                rec.timestamp  = std::chrono::system_clock::now();
                co_yield std::move(rec);
            }
        } catch (const std::exception& e) {
            std::println("[MarketStack] parse error: {}", e.what());
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "MarketStack"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{300};
    }

private:
    std::string api_key_;
    HttpClient  http_;
};

} // namespace macro
