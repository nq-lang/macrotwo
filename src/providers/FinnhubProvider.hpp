#pragma once
// src/providers/FinnhubProvider.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Finnhub provider.
// Feeds: Table 6.2.2 (Central Bank Updates), 6.2.6 (News), 6.2.7 (Econ Calendar)
// API docs: https://finnhub.io/docs/api
// ─────────────────────────────────────────────────────────────────────────────
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>
#include <chrono>
#include <string>

namespace macro {

class FinnhubProvider final : public IDataProvider {
public:
    explicit FinnhubProvider(std::string api_key)
        : api_key_(std::move(api_key)) {}

    std::expected<void, ProviderError> connect() override {
        auto resp = http_.get(
            std::format("https://finnhub.io/api/v1/news?category=general&token={}", api_key_));
        if (!resp) return std::unexpected(resp.error());
        std::println("[Finnhub] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // ── Market-moving news ────────────────────────────────────────────
        auto news_resp = http_.get(
            std::format("https://finnhub.io/api/v1/news?category=general&minId=0&token={}", api_key_));
        if (news_resp) {
            for (auto rec : parse_news(news_resp->body))
                co_yield std::move(rec);
        }

        // ── Economic calendar (central bank events) ────────────────────────
        auto today = get_date_str(0);
        auto week  = get_date_str(7);
        auto cal_resp = http_.get(
            std::format("https://finnhub.io/api/v1/calendar/economic?from={}&to={}&token={}",
                        today, week, api_key_));
        if (cal_resp) {
            for (auto rec : parse_econ_calendar(cal_resp->body))
                co_yield std::move(rec);
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "Finnhub"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{60};
    }

private:
    std::string api_key_;
    HttpClient  http_;

    static std::string get_date_str(int days_offset) {
        auto tp  = std::chrono::system_clock::now() + std::chrono::hours(24 * days_offset);
        auto tt  = std::chrono::system_clock::to_time_t(tp);
        std::tm gmt{};
#ifdef _WIN32
        gmtime_s(&gmt, &tt);
#else
        gmtime_r(&tt, &gmt);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &gmt);
        return buf;
    }

    std::vector<NormalizedRecord> parse_news(const std::string& body) {
        std::vector<NormalizedRecord> out;
        try {
            auto j = nlohmann::json::parse(body);
            for (auto& item : j) {
                NormalizedRecord rec;
                rec.record_id   = std::format("finnhub:news:{}", item.value("id", 0));
                rec.domain      = "news";
                rec.source_name = "Finnhub";
                rec.headline    = item.value("headline", "");
                if (rec.headline.size() > 120) rec.headline.resize(120);
                rec.payload_json = item.dump();
                rec.severity     = 1;
                rec.timestamp    = std::chrono::system_clock::now();
                out.push_back(std::move(rec));
            }
        } catch (...) {}
        return out;
    }

    std::vector<NormalizedRecord> parse_econ_calendar(const std::string& body) {
        std::vector<NormalizedRecord> out;
        try {
            auto j = nlohmann::json::parse(body);
            if (!j.contains("economicCalendar")) return out;
            for (auto& item : j["economicCalendar"]) {
                NormalizedRecord rec;
                std::string event   = item.value("event", "");
                std::string country = item.value("country", "");
                std::string impact  = item.value("impact", "low");

                rec.record_id    = std::format("finnhub:econ:{}:{}", country, event);
                rec.domain       = "econ_calendar";
                rec.source_name  = "Finnhub";
                rec.headline     = std::format("[{}] {}", country, event);
                rec.payload_json = item.dump();
                rec.geo.country_iso2 = country;
                rec.severity     = impact == "high" ? 3 : (impact == "medium" ? 2 : 1);
                rec.timestamp    = std::chrono::system_clock::now();
                out.push_back(std::move(rec));
            }
        } catch (...) {}
        return out;
    }
};

} // namespace macro
