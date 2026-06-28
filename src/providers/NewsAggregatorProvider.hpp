#pragma once
// src/providers/NewsAggregatorProvider.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Fused news provider across four licensed news APIs:
//   - NewsAPI.org       (https://newsapi.org/docs)
//   - GNews API         (https://gnews.io/docs)
//   - NewsData.io       (https://newsdata.io/docs)
//   - World News API    (https://worldnewsapi.com/docs)
//
// All accessed via their official REST APIs — no scraping.
// Feeds: Table 6.2.6 (Critical Market News), Table 6.2.1 (Geopolitics)
// ─────────────────────────────────────────────────────────────────────────────
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>
#include <vector>

namespace macro {

class NewsAggregatorProvider final : public IDataProvider {
public:
    struct Config {
        std::string newsapi_key;
        std::string gnews_key;
        std::string newsdataio_key;
        std::string worldnewsapi_key;
    };

    explicit NewsAggregatorProvider(Config cfg)
        : cfg_(std::move(cfg)) {}

    std::expected<void, ProviderError> connect() override {
        // Quick connectivity check against NewsAPI
        if (!cfg_.newsapi_key.empty()) {
            auto resp = http_.get(
                std::format("https://newsapi.org/v2/top-headlines?category=business&pageSize=1&apiKey={}",
                            cfg_.newsapi_key));
            if (!resp) {
                std::println("[NewsAgg] NewsAPI check failed: {}", resp.error().message);
                // Non-fatal: other sources may still work
            }
        }
        std::println("[NewsAgg] connected");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // Fetch from each source and yield results
        if (!cfg_.newsapi_key.empty()) {
            for (auto& r : fetch_newsapi()) co_yield r;
        }
        if (!cfg_.gnews_key.empty()) {
            for (auto& r : fetch_gnews()) co_yield r;
        }
        if (!cfg_.newsdataio_key.empty()) {
            for (auto& r : fetch_newsdataio()) co_yield r;
        }
        if (!cfg_.worldnewsapi_key.empty()) {
            for (auto& r : fetch_worldnewsapi()) co_yield r;
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return "NewsAggregator";
    }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{120};
    }

private:
    Config     cfg_;
    HttpClient http_;

    // ── Criticality heuristic: scan headline for macro-relevant keywords ──
    static int assess_criticality(const std::string& headline) {
        static constexpr const char* HIGH_KEYWORDS[] = {
            "central bank", "fed ", "ecb ", "rate decision", "emergency",
            "crisis", "collapse", "sanctions", "military", "war"
        };
        static constexpr const char* MED_KEYWORDS[] = {
            "inflation", "gdp", "recession", "unemployment", "cpi",
            "treasury", "yield", "spread", "earnings"
        };
        std::string hl_lower = headline;
        std::ranges::transform(hl_lower, hl_lower.begin(), ::tolower);
        for (auto* kw : HIGH_KEYWORDS)
            if (hl_lower.find(kw) != std::string::npos) return 3;
        for (auto* kw : MED_KEYWORDS)
            if (hl_lower.find(kw) != std::string::npos) return 2;
        return 1;
    }

    std::vector<NormalizedRecord> fetch_newsapi() {
        std::vector<NormalizedRecord> out;
        auto resp = http_.get(
            std::format("https://newsapi.org/v2/top-headlines"
                        "?category=business&pageSize=20&language=en&apiKey={}",
                        cfg_.newsapi_key));
        if (!resp) return out;
        try {
            auto j = nlohmann::json::parse(resp->body);
            for (auto& a : j.value("articles", nlohmann::json::array())) {
                NormalizedRecord rec;
                rec.record_id    = std::format("newsapi:{}", a.value("url", ""));
                rec.domain       = "news";
                rec.source_name  = a.contains("source") ? a["source"].value("name","NewsAPI") : "NewsAPI";
                rec.headline     = a.value("title", "");
                if (rec.headline.size() > 120) rec.headline.resize(120);
                rec.payload_json = a.dump();
                rec.severity     = assess_criticality(rec.headline);
                rec.timestamp    = std::chrono::system_clock::now();
                out.push_back(std::move(rec));
            }
        } catch (...) {}
        return out;
    }

    std::vector<NormalizedRecord> fetch_gnews() {
        std::vector<NormalizedRecord> out;
        auto resp = http_.get(
            std::format("https://gnews.io/api/v4/top-headlines"
                        "?category=business&lang=en&max=10&token={}",
                        cfg_.gnews_key));
        if (!resp) return out;
        try {
            auto j = nlohmann::json::parse(resp->body);
            for (auto& a : j.value("articles", nlohmann::json::array())) {
                NormalizedRecord rec;
                rec.record_id    = std::format("gnews:{}", a.value("url", ""));
                rec.domain       = "news";
                rec.source_name  = "GNews";
                rec.headline     = a.value("title", "");
                if (rec.headline.size() > 120) rec.headline.resize(120);
                rec.payload_json = a.dump();
                rec.severity     = assess_criticality(rec.headline);
                rec.timestamp    = std::chrono::system_clock::now();
                out.push_back(std::move(rec));
            }
        } catch (...) {}
        return out;
    }

    std::vector<NormalizedRecord> fetch_newsdataio() {
        std::vector<NormalizedRecord> out;
        auto resp = http_.get(
            std::format("https://newsdata.io/api/1/news"
                        "?category=business,politics&language=en&apikey={}",
                        cfg_.newsdataio_key));
        if (!resp) return out;
        try {
            auto j = nlohmann::json::parse(resp->body);
            for (auto& a : j.value("results", nlohmann::json::array())) {
                NormalizedRecord rec;
                rec.record_id    = std::format("newsdataio:{}", a.value("article_id", ""));
                rec.domain       = "news";
                rec.source_name  = "NewsData.io";
                rec.headline     = a.value("title", "");
                if (rec.headline.size() > 120) rec.headline.resize(120);
                rec.payload_json = a.dump();
                // geo from country field
                if (a.contains("country") && a["country"].is_array() && !a["country"].empty())
                    rec.geo.country_iso2 = a["country"][0].get<std::string>();
                rec.severity     = assess_criticality(rec.headline);
                rec.timestamp    = std::chrono::system_clock::now();
                out.push_back(std::move(rec));
            }
        } catch (...) {}
        return out;
    }

    std::vector<NormalizedRecord> fetch_worldnewsapi() {
        std::vector<NormalizedRecord> out;
        auto resp = http_.get(
            std::format("https://api.worldnewsapi.com/search-news"
                        "?text=economy+finance+central+bank&number=10&api-key={}",
                        cfg_.worldnewsapi_key),
            {{"x-api-key", cfg_.worldnewsapi_key}});
        if (!resp) return out;
        try {
            auto j = nlohmann::json::parse(resp->body);
            for (auto& a : j.value("news", nlohmann::json::array())) {
                NormalizedRecord rec;
                rec.record_id    = std::format("worldnews:{}", a.value("id", 0));
                rec.domain       = "news";
                rec.source_name  = "WorldNewsAPI";
                rec.headline     = a.value("title", "");
                if (rec.headline.size() > 120) rec.headline.resize(120);
                rec.payload_json = a.dump();
                rec.severity     = assess_criticality(rec.headline);
                rec.timestamp    = std::chrono::system_clock::now();
                out.push_back(std::move(rec));
            }
        } catch (...) {}
        return out;
    }
};

} // namespace macro
