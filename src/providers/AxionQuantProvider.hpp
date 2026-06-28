#pragma once
// src/providers/AxionQuantProvider.hpp
// AxionQuant API — quantitative signals, factor scores, alternative data
// Feeds: Layer 3 Regime Score augmentation, Table 6.2.4 positioning
// API docs: https://axionquant.io (commercial)
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>

namespace macro {

class AxionQuantProvider final : public IDataProvider {
public:
    explicit AxionQuantProvider(std::string api_key)
        : api_key_(std::move(api_key)) {}

    std::expected<void, ProviderError> connect() override {
        // AxionQuant uses x-api-key header auth
        auto r = http_.get(
            "https://api.axionquant.io/v1/health",
            {{"x-api-key", api_key_}});
        if (!r) {
            // Endpoint may vary — treat connection as best-effort
            std::println("[AxionQuant] health check failed ({}), continuing",
                         r.error().message);
            return {}; // non-fatal
        }
        std::println("[AxionQuant] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // ── Factor signal pull ─────────────────────────────────────────────
        auto r = http_.get(
            "https://api.axionquant.io/v1/signals/macro",
            {{"x-api-key", api_key_}});

        if (!r) { co_return; }

        try {
            auto j = nlohmann::json::parse(r->body);
            for (auto& [key, val] : j.items()) {
                NormalizedRecord rec;
                rec.record_id    = std::format("axionquant:signal:{}", key);
                rec.domain       = "sector_data";
                rec.source_name  = "AxionQuant";
                rec.headline     = std::format("AXQ {} signal: {}", key,
                    val.is_number() ? std::to_string(val.get<float>()) : val.dump());
                rec.payload_json = nlohmann::json{{key, val}}.dump();
                rec.geo.country_iso2 = "US";
                rec.severity     = 1;
                rec.timestamp    = std::chrono::system_clock::now();
                co_yield std::move(rec);
            }
        } catch (const std::exception& e) {
            std::println("[AxionQuant] parse error: {}", e.what());
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "AxionQuant"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{300};
    }

private:
    std::string api_key_;
    HttpClient  http_;
};

} // namespace macro
