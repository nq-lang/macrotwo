#pragma once
// src/providers/PolygonProvider.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Polygon.io provider.
// Feeds: Table 6.2.4 (Institutional Positioning — via ticker snapshot/news),
//        Table 6.2.6 (Critical Market News)
// API docs: https://polygon.io/docs/
// ─────────────────────────────────────────────────────────────────────────────
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>

namespace macro {

class PolygonProvider final : public IDataProvider {
public:
    explicit PolygonProvider(std::string api_key)
        : api_key_(std::move(api_key)) {}

    std::expected<void, ProviderError> connect() override {
        auto resp = http_.get(
            std::format("https://api.polygon.io/v2/aggs/ticker/SPY/prev?adjusted=true&apiKey={}",
                        api_key_));
        if (!resp) return std::unexpected(resp.error());
        std::println("[Polygon] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // ── Market news ───────────────────────────────────────────────────
        auto resp = http_.get(
            std::format("https://api.polygon.io/v2/reference/news?limit=50&order=desc&apiKey={}",
                        api_key_));
        if (!resp) co_return;

        try {
            auto j = nlohmann::json::parse(resp->body);
            if (!j.contains("results")) co_return;
            for (auto& item : j["results"]) {
                NormalizedRecord rec;
                rec.record_id    = std::format("polygon:news:{}", item.value("id", ""));
                rec.domain       = "news";
                rec.source_name  = "Polygon.io";
                rec.headline     = item.value("title", "");
                if (rec.headline.size() > 120) rec.headline.resize(120);
                rec.payload_json = item.dump();
                rec.severity     = 1;
                rec.timestamp    = std::chrono::system_clock::now();

                // Tag tickers from the article
                if (item.contains("tickers") && !item["tickers"].empty())
                    rec.headline += " [" + item["tickers"][0].get<std::string>() + "]";

                co_yield std::move(rec);
            }
        } catch (...) {}
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "Polygon.io"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{120};
    }

private:
    std::string api_key_;
    HttpClient  http_;
};

} // namespace macro
