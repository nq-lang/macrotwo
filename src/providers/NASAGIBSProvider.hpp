#pragma once
// src/providers/NASAGIBSProvider.hpp
// NASA GIBS (Global Imagery Browse Services) — licensed satellite imagery tiles
// Feeds: Ground Intelligence pane (§5.6) — satellite/GEOINT basemap
// API docs: https://nasa-gibs.github.io/gibs-api-docs/
// Key: tdZ0cU2Bai2OffWIqDetqJ6zjl9B4aq4ONwWklCt
//
// GIBS provides WMTS (Web Map Tile Service) — tiles are fetched by
// TileMatrixSet/Layer/TileMatrix/TileRow/TileCol.
// We also surface FIRMS (Fire Information for Resource Management System)
// and MODIS Aerosol/Vegetation layers as alt-data overlays.
#include "IDataProvider.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <print>
#include <string>
#include <vector>

namespace macro {

// Describes a fetched tile for display in the Ground Intelligence pane
struct SatelliteTile {
    std::string layer;
    int         zoom{0};
    int         tile_x{0};
    int         tile_y{0};
    std::string date;       // YYYY-MM-DD
    std::vector<uint8_t> png_bytes;  // raw PNG tile data
    double      bbox_west{0}, bbox_south{0}, bbox_east{0}, bbox_north{0};
};

class NASAGIBSProvider final : public IDataProvider {
public:
    explicit NASAGIBSProvider(std::string api_key)
        : api_key_(std::move(api_key)) {}

    std::expected<void, ProviderError> connect() override {
        // GIBS WMTS GetCapabilities to validate connectivity
        auto r = http_.get(
            "https://gibs.earthdata.nasa.gov/wmts/epsg4326/best/wmts.cgi"
            "?SERVICE=WMTS&REQUEST=GetCapabilities");
        if (!r) return std::unexpected(r.error());
        std::println("[NASA GIBS] connected OK (capabilities fetched)");
        return {};
    }

    std::generator<NormalizedRecord> poll() override {
        // GIBS is primarily a tile server — we emit metadata records here;
        // actual tile fetching is done on-demand by the Ground Intel pane
        // when the user navigates to GeoResolution::Ground.
        //
        // Here we emit availability notifications for key layers.
        struct LayerInfo {
            const char* id;
            const char* label;
            const char* category; // "basemap" | "fires" | "vegetation" | "aerosol"
        };
        static constexpr LayerInfo LAYERS[] = {
            {"VIIRS_SNPP_TrueColor_143m",
             "VIIRS True Color (143m)",  "basemap"},
            {"MODIS_Terra_CorrectedReflectance_TrueColor",
             "MODIS Terra True Color",   "basemap"},
            {"FIRMS_VIIRS_SNPP_NRT",
             "Active Fire Detections (VIIRS)",  "fires"},
            {"MODIS_Terra_NDVI_8Day",
             "MODIS NDVI (Vegetation)",  "vegetation"},
            {"MERRA2_Total_Aerosol_Optical_Thickness_550nm_Daily",
             "Aerosol Optical Thickness (MERRA-2)", "aerosol"},
            {"MODIS_Aqua_L3_SST_MidIR_9km_Night_Daily",
             "Sea Surface Temperature (MODIS Aqua)", "sst"},
        };

        // Get today's date for tile requests
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        std::tm gmt{};
#ifdef _WIN32
        gmtime_s(&gmt, &tt);
#else
        gmtime_r(&tt, &gmt);
#endif
        char date_buf[12];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &gmt);

        for (const auto& layer : LAYERS) {
            NormalizedRecord rec;
            rec.record_id    = std::format("gibs:layer:{}", layer.id);
            rec.domain       = "geopolitics"; // alt-data / GEOINT
            rec.source_name  = "NASA GIBS";
            rec.headline     = std::format("[GIBS] {} available for {}",
                                           layer.label, date_buf);
            rec.payload_json = std::format(
                "{{\"layer_id\":\"{}\",\"label\":\"{}\",\"category\":\"{}\","
                "\"date\":\"{}\",\"wmts_url\":\"https://gibs.earthdata.nasa.gov/"
                "wmts/epsg4326/best/{}/default/{}/250m/{{z}}/{{y}}/{{x}}.jpg\","
                "\"api_key\":\"{}\"}}",
                layer.id, layer.label, layer.category, date_buf,
                layer.id, date_buf, api_key_);
            rec.severity     = 0;
            rec.timestamp    = std::chrono::system_clock::now();
            co_yield std::move(rec);
        }
    }

    // ── Tile fetch utility (called by Ground Intel pane on demand) ─────────
    // Returns the WMTS tile URL for a given layer/zoom/tile coords
    [[nodiscard]] std::string tile_url(std::string_view layer_id,
                                       int zoom, int tile_x, int tile_y,
                                       std::string_view date) const {
        return std::format(
            "https://gibs.earthdata.nasa.gov/wmts/epsg4326/best"
            "/{}/default/{}/250m/{}/{}/{}.jpg",
            layer_id, date, zoom, tile_y, tile_x);
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "NASA GIBS"; }
    [[nodiscard]] std::chrono::seconds poll_interval() const noexcept override {
        return std::chrono::seconds{3600}; // new imagery available ~daily
    }

private:
    std::string api_key_;
    HttpClient  http_;
};

} // namespace macro
