#pragma once
// src/app/Secrets.hpp
// All API keys loaded exclusively from environment variables.
// No literal key ever appears in source code.
#include <cstdlib>
#include <expected>
#include <string>
#include <vector>
#include <print>
#include <algorithm>
#include <cctype>

namespace macro {

struct Secrets {
    // Market data
    std::string fred_api_key;
    std::string alpha_vantage_api_key;
    std::string polygon_api_key;
    std::string marketstack_api_key;
    std::string tradier_api_key;
    std::string finnhub_api_key;
    std::string axionquant_api_key;

    // News / Sentiment (all 4 used by GeoScopedFetcher)
    std::string newsapi_api_key;
    std::string gnews_api_key;
    std::string newsdataio_api_key;
    std::string worldnewsapi_api_key;

    // LLM — Section 3 rationale pipeline
    std::string anthropic_api_key;

    // Remote sensing
    std::string nasa_gibs_api_key;

    // Optional
    std::string mapbox_api_key;
    std::string gee_project_id;
    std::string gee_service_account_json;
};

struct MissingSecrets {
    std::vector<std::string> missing_vars;
    std::vector<std::string> optional_missing;
};

[[nodiscard]] inline std::expected<Secrets, MissingSecrets> load_secrets() {
    Secrets s;
    MissingSecrets err;

    auto get = [&](const char* var, std::string& dest, bool required = true) {
        const char* v = std::getenv(var);
        if (v && v[0] != '\0') { dest = v; }
        else if (required) err.missing_vars.emplace_back(var);
        else               err.optional_missing.emplace_back(var);
    };

    // Required
    get("FRED_API_KEY",             s.fred_api_key);
    get("ALPHA_VANTAGE_API_KEY",    s.alpha_vantage_api_key);
    get("POLYGON_API_KEY",          s.polygon_api_key);
    get("MARKETSTACK_API_KEY",      s.marketstack_api_key);
    get("TRADIER_API_KEY",          s.tradier_api_key);
    get("FINNHUB_API_KEY",          s.finnhub_api_key);
    get("AXIONQUANT_API_KEY",       s.axionquant_api_key);
    get("NEWSAPI_API_KEY",          s.newsapi_api_key);
    get("GNEWS_API_KEY",            s.gnews_api_key);
    get("NEWSDATAIO_API_KEY",       s.newsdataio_api_key);
    get("WORLDNEWSAPI_API_KEY",     s.worldnewsapi_api_key);
    get("ANTHROPIC_API_KEY",        s.anthropic_api_key);
    get("NASA_GIBS_API_KEY",        s.nasa_gibs_api_key);

    // Optional
    get("MAPBOX_API_KEY",           s.mapbox_api_key,           false);
    get("GEE_PROJECT_ID",           s.gee_project_id,           false);
    get("GEE_SERVICE_ACCOUNT_JSON", s.gee_service_account_json, false);

    if (!err.missing_vars.empty()) {
        for (auto& v : err.missing_vars)
            std::println("[Secrets] MISSING: {}", v);
        return std::unexpected(std::move(err));
    }
    if (!err.optional_missing.empty())
        for (auto& v : err.optional_missing)
            std::println("[Secrets] optional not set: {}", v);

    return s;
}

} // namespace macro
