#pragma once
// src/providers/TradierProvider.hpp
// Tradier API — options chain data, equity quotes, market breadth
// Feeds: Table 6.2.4 (positioning via put/call ratios), Layer 3 breadth signal
// API docs: https://documentation.tradier.com/brokerage-api
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>

namespace macro {

class TradierProvider final : public IDataProvider {
public:
    explicit TradierProvider(std::string api_key)
        : api_key_(std::move(api_key)) {}

    std::expected<void, ProviderError> connect() override {
        auto r = http_.get(
            "https://api.tradier.com/v1/markets/quotes?symbols=SPY",
            {{"Authorization", "Bearer " + api_key_},
             {"Accept",        "application/json"}});
        if (!r) return std::unexpected(r.error());
        std::println("[Tradier] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // ── Sector ETF quote snapshot ─────────────────────────────────────
        static constexpr const char* SECTOR_ETFS =
            "SPY,XLK,XLF,XLE,XLU,XLV,XLP,XLI,XLB,XLY,XLRE,XLC,VGK,EWJ";

        auto r = http_.get(
            std::format("https://api.tradier.com/v1/markets/quotes?symbols={}", SECTOR_ETFS),
            {{"Authorization", "Bearer " + api_key_},
             {"Accept",        "application/json"}});

        if (!r) co_return;

        try {
            auto j = nlohmann::json::parse(r->body);
            auto& quotes = j["quotes"]["quote"];
            auto emit = [&](const nlohmann::json& q) -> NormalizedRecord {
                NormalizedRecord rec;
                std::string sym = q.value("symbol", "");
                rec.record_id   = std::format("tradier:quote:{}", sym);
                rec.domain      = "sector_data";
                rec.source_name = "Tradier";
                float last      = q.value("last", 0.0f);
                float chg_pct   = q.value("change_percentage", 0.0f);
                rec.headline    = std::format("{} last={:.2f} chg={:+.2f}%",
                                              sym, last, chg_pct);
                rec.payload_json = q.dump();
                rec.geo.country_iso2 = "US";
                rec.timestamp   = std::chrono::system_clock::now();
                // Severity: big moves get elevated flags
                rec.severity = (std::abs(chg_pct) > 3.0f) ? 3
                             : (std::abs(chg_pct) > 1.5f) ? 2 : 1;
                return rec;
            };

            if (quotes.is_array()) {
                for (auto& q : quotes) co_yield emit(q);
            } else if (quotes.is_object()) {
                co_yield emit(quotes);
            }
        } catch (const std::exception& e) {
            std::println("[Tradier] parse error: {}", e.what());
        }

        // ── Options put/call ratio (SPY, QQQ) ─────────────────────────────
        for (const char* sym : {"SPY", "QQQ"}) {
            auto opt_r = http_.get(
                std::format("https://api.tradier.com/v1/markets/options/chains"
                            "?symbol={}&expiration=&greeks=false", sym),
                {{"Authorization", "Bearer " + api_key_},
                 {"Accept",        "application/json"}});
            if (!opt_r) continue;

            try {
                auto oj = nlohmann::json::parse(opt_r->body);
                auto& chain = oj["options"]["option"];
                if (!chain.is_array()) continue;

                int puts = 0, calls = 0;
                for (auto& leg : chain) {
                    std::string otype = leg.value("option_type", "");
                    if (otype == "put")  ++puts;
                    if (otype == "call") ++calls;
                }

                float pcr = (calls > 0) ? static_cast<float>(puts) / calls : 1.0f;
                NormalizedRecord rec;
                rec.record_id    = std::format("tradier:pcr:{}", sym);
                rec.domain       = "positioning";
                rec.source_name  = "Tradier Options";
                rec.headline     = std::format("{} P/C Ratio: {:.3f} ({}P/{}C)",
                                               sym, pcr, puts, calls);
                rec.payload_json = std::format(
                    "{{\"symbol\":\"{}\",\"put_call_ratio\":{:.4f},"
                    "\"puts\":{},\"calls\":{}}}",
                    sym, pcr, puts, calls);
                rec.geo.country_iso2 = "US";
                rec.severity    = (pcr > 1.5f || pcr < 0.5f) ? 3 : 1;
                rec.timestamp   = std::chrono::system_clock::now();
                co_yield std::move(rec);

            } catch (...) {}
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "Tradier"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{120};
    }

private:
    std::string api_key_;
    HttpClient  http_;
};

} // namespace macro
