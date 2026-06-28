#pragma once
// src/providers/OpenMeteoProvider.hpp
// Open-Meteo — free, no API key required
// Feeds: Ground Intelligence pane weather overlays, alternative data
//        signals (agricultural commodity context, energy demand proxies)
// API docs: https://open-meteo.com/en/docs
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>
#include <array>

namespace macro {

class OpenMeteoProvider final : public IDataProvider {
public:
    OpenMeteoProvider() = default;

    std::expected<void, ProviderError> connect() override {
        // Quick test with NYC coordinates
        auto r = http_.get(
            "https://api.open-meteo.com/v1/forecast"
            "?latitude=40.71&longitude=-74.01&current_weather=true&hourly=temperature_2m"
            "&forecast_days=1");
        if (!r) return std::unexpected(r.error());
        std::println("[Open-Meteo] connected OK");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // Key financial hub cities for weather + energy demand signals
        struct City { const char* name; const char* iso2; double lat; double lon; };
        static constexpr City CITIES[] = {
            {"New York",   "US",  40.71, -74.01},
            {"London",     "GB",  51.51,  -0.13},
            {"Tokyo",      "JP",  35.68, 139.69},
            {"Frankfurt",  "DE",  50.11,   8.68},
            {"Shanghai",   "CN",  31.23, 121.47},
            {"Dubai",      "AE",  25.20,  55.27},
            {"Singapore",  "SG",   1.35, 103.82},
            {"Hong Kong",  "HK",  22.32, 114.17},
            {"Riyadh",     "SA",  24.69,  46.72},
            {"Moscow",     "RU",  55.75,  37.62},
        };

        for (const auto& city : CITIES) {
            std::string url = std::format(
                "https://api.open-meteo.com/v1/forecast"
                "?latitude={:.2f}&longitude={:.2f}"
                "&current_weather=true"
                "&hourly=temperature_2m,precipitation,windspeed_10m"
                "&daily=temperature_2m_max,temperature_2m_min,precipitation_sum"
                "&forecast_days=3&timezone=auto",
                city.lat, city.lon);

            auto r = http_.get(url);
            if (!r) continue;

            try {
                auto j = nlohmann::json::parse(r->body);
                float temp = 0.0f;
                std::string condition;

                if (j.contains("current_weather")) {
                    auto& cw = j["current_weather"];
                    temp      = cw.value("temperature", 0.0f);
                    int wc    = cw.value("weathercode", 0);
                    condition = weathercode_to_label(wc);
                }

                NormalizedRecord rec;
                rec.record_id    = std::format("openmeteo:{}", city.name);
                rec.domain       = "geopolitics"; // categorised as alt-data/geo
                rec.source_name  = "Open-Meteo";
                rec.headline     = std::format("[WEATHER] {} {:.1f}°C {}",
                                               city.name, temp, condition);
                rec.payload_json = r->body.substr(0, 2048);
                rec.geo.country_iso2 = city.iso2;
                rec.geo.lat          = city.lat;
                rec.geo.lon          = city.lon;
                rec.geo.city         = city.name;
                rec.severity         = 0; // informational
                rec.timestamp        = std::chrono::system_clock::now();
                co_yield std::move(rec);

            } catch (...) {}
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "Open-Meteo"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{900}; // 15 min — weather doesn't need faster
    }

private:
    HttpClient http_;

    static std::string weathercode_to_label(int wc) {
        if (wc == 0)                        return "Clear";
        if (wc <= 3)                        return "Cloudy";
        if (wc <= 9)                        return "Fog";
        if (wc >= 51 && wc <= 67)           return "Rain";
        if (wc >= 71 && wc <= 77)           return "Snow";
        if (wc >= 80 && wc <= 82)           return "Showers";
        if (wc >= 95 && wc <= 99)           return "Thunderstorm";
        return "Mixed";
    }
};

} // namespace macro
