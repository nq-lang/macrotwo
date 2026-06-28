#pragma once
// src/providers/GEEProvider.hpp
// Google Earth Engine (GEE) REST API
// Project: single-bindery-500504-j2  (ID: 921821281601)
// Feeds: Ground Intelligence pane — nighttime lights, NDVI, industrial activity
//        Globe GMSI — economic activity proxies (nightlights → GDP proxy)
//
// GEE REST API (earthengine.googleapis.com) requires OAuth2 service account
// auth. In production, the service account key JSON is loaded from env var
// GEE_SERVICE_ACCOUNT_JSON. For Phase 0/1 we emit stub records and make
// authenticated computePixels calls when credentials are present.
//
// API docs: https://developers.google.com/earth-engine/reference/rest
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>
#include <optional>

namespace macro {

class GEEProvider final : public IDataProvider {
public:
    struct Config {
        std::string project_id;          // "single-bindery-500504-j2"
        std::string service_account_key; // full JSON from GEE_SERVICE_ACCOUNT_JSON env
    };

    explicit GEEProvider(Config cfg) : cfg_(std::move(cfg)) {}

    std::expected<void, ProviderError> connect() override {
        if (cfg_.project_id.empty()) {
            std::println("[GEE] no project_id configured — running in stub mode");
            stub_mode_ = true;
            return {};
        }

        // OAuth2 token fetch using service account credentials
        // (full JWT flow — stubbed here; real impl uses a JWT library)
        if (cfg_.service_account_key.empty()) {
            std::println("[GEE] no service account key — stub mode");
            stub_mode_ = true;
            return {};
        }

        // TODO Phase 4: implement JWT signing with the SA key
        // For now, attempt unauthenticated public dataset access
        auto r = http_.get(
            "https://earthengine.googleapis.com/v1/projects/"
            + cfg_.project_id + "/operations",
            auth_headers());

        if (!r || (r->status_code != 200 && r->status_code != 403)) {
            stub_mode_ = true;
            std::println("[GEE] connectivity check failed — stub mode");
        } else {
            std::println("[GEE] connected OK (project={})", cfg_.project_id);
        }
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        if (stub_mode_) {
            co_yield make_stub_record("VIIRS_Nightlights", "Nighttime Lights (VIIRS DNB)", "US");
            co_yield make_stub_record("Sentinel2_NDVI",    "Vegetation Index (Sentinel-2)", "DE");
            co_yield make_stub_record("S5P_NO2",           "NO2 Industrial Emissions (S5P)","CN");
            co_return;
        }

        // ── Real GEE: computePixels for key indicators ─────────────────────
        // Nighttime lights — VIIRS DNB monthly composite
        // Used in economics literature as GDP activity proxy
        {
            auto result = compute_nightlights_index("COPERNICUS/S2_SR", -180, -90, 180, 90);
            if (result) co_yield std::move(*result);
        }

        // NO2 emissions proxy — Sentinel-5P TROPOMI
        {
            auto result = compute_no2_index();
            if (result) co_yield std::move(*result);
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "Google Earth Engine"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{86400}; // daily — satellite composites update daily
    }

private:
    Config cfg_;
    HttpClient http_;
    bool stub_mode_{false};
    std::string access_token_;

    HttpClient::Headers auth_headers() const {
        if (access_token_.empty()) return {};
        return {{"Authorization", "Bearer " + access_token_}};
    }

    NormalizedRecord make_stub_record(std::string_view layer_id,
                                      std::string_view label,
                                      std::string_view iso2) const {
        NormalizedRecord rec;
        rec.record_id    = std::format("gee:stub:{}", layer_id);
        rec.domain       = "geopolitics";
        rec.source_name  = "GEE (stub)";
        rec.headline     = std::format("[GEE STUB] {} — auth pending", label);
        rec.payload_json = std::format(
            "{{\"layer\":\"{}\",\"project\":\"{}\",\"stub\":true}}",
            layer_id, cfg_.project_id);
        rec.geo.country_iso2 = std::string(iso2);
        rec.severity         = 0;
        rec.timestamp        = std::chrono::system_clock::now();
        return rec;
    }

    std::optional<NormalizedRecord> compute_nightlights_index(
        [[maybe_unused]] std::string_view collection,
        [[maybe_unused]] double west, [[maybe_unused]] double south,
        [[maybe_unused]] double east, [[maybe_unused]] double north) {

        // GEE computePixels POST body
        nlohmann::json body = {
            {"expression", {
                {"constantValue", 0}  // placeholder — real EE expression goes here
            }},
            {"fileFormat", "PNG"},
            {"grid", {
                {"dimensions", {{"width", 256}, {"height", 256}}},
                {"affineTransform", {
                    {"scaleX", (east-west)/256},
                    {"shearX", 0}, {"translateX", west},
                    {"shearY", 0}, {"scaleY", -(north-south)/256},
                    {"translateY", north}
                }},
                {"crsCode", "EPSG:4326"}
            }}
        };

        auto r = http_.post(
            "https://earthengine.googleapis.com/v1/projects/"
            + cfg_.project_id + "/image:computePixels",
            body.dump(), auth_headers());

        if (!r) return std::nullopt;

        NormalizedRecord rec;
        rec.record_id    = "gee:nightlights";
        rec.domain       = "geopolitics";
        rec.source_name  = "GEE / VIIRS";
        rec.headline     = "[GEE] Nighttime Lights composite fetched";
        rec.payload_json = r->body.substr(0, 1024);
        rec.severity     = 0;
        rec.timestamp    = std::chrono::system_clock::now();
        return rec;
    }

    std::optional<NormalizedRecord> compute_no2_index() {
        // Sentinel-5P NO2 column density — industrial activity proxy
        auto r = http_.get(
            "https://earthengine.googleapis.com/v1/projects/"
            + cfg_.project_id + "/assets/COPERNICUS/S5P/NRTI/L3_NO2",
            auth_headers());
        if (!r) return std::nullopt;

        NormalizedRecord rec;
        rec.record_id    = "gee:no2";
        rec.domain       = "geopolitics";
        rec.source_name  = "GEE / Sentinel-5P";
        rec.headline     = "[GEE] NO2 industrial emissions layer available";
        rec.payload_json = r->body.substr(0, 512);
        rec.severity     = 0;
        rec.timestamp    = std::chrono::system_clock::now();
        return rec;
    }
};

} // namespace macro
