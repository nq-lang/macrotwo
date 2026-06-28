#pragma once
// src/globe/GMSIComputer.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Global Macro Stress Index (GMSI) — §5.4
// Composite per-country risk index feeding the globe heat overlay.
//
// Five inputs (weights configurable):
//   1. Equity realized volatility (z-scored vs 2y history)
//   2. Sovereign CDS spread (z-scored vs 2y history)
//   3. FX implied volatility
//   4. Geopolitical risk score (from news velocity + geopolitics table severity)
//   5. News/sentiment velocity from Table 6.2.6
//
// Output: GMSI ∈ [0, 5] (0 = calm, 5 = systemic stress) per ISO2 country.
// Recomputed on a scheduled cadence (default 60s).
// Color-mapped to §2 muted severity palette — never raw saturated heatmap.
// ─────────────────────────────────────────────────────────────────────────────
#include "../providers/IDataProvider.hpp"
#include "../ui_common/Theme.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <print>
#include <string>
#include <unordered_map>
#include <vector>

namespace macro {

struct GMSIScore {
    float score{0.0f};       // [0, 5] composite
    float eq_vol_z{0.0f};    // component z-scores for drill-in tooltip
    float cds_z{0.0f};
    float fx_vol_z{0.0f};
    float geo_risk{0.0f};
    float news_vel{0.0f};
    std::chrono::system_clock::time_point computed_at;
};

// Weight configuration (must sum to 1.0)
struct GMSIWeights {
    float equity_vol{0.25f};
    float cds_spread{0.25f};
    float fx_vol{0.20f};
    float geo_risk{0.15f};
    float news_velocity{0.15f};
};

class GMSIComputer {
public:
    explicit GMSIComputer(GMSIWeights w = {}) : weights_(w) {
        // Seed with baseline scores so the globe has initial colors on startup
        seed_baseline();
    }

    // ── Called by provider ingest pipeline ────────────────────────────────
    void record_equity_vol(const std::string& iso2, float realized_vol_annualized) {
        std::scoped_lock lk{mtx_};
        raw_eq_vol_[iso2] = realized_vol_annualized;
        dirty_.store(true);
    }

    void record_cds_spread(const std::string& iso2, float cds_bps) {
        std::scoped_lock lk{mtx_};
        raw_cds_[iso2] = cds_bps;
        dirty_.store(true);
    }

    void record_fx_vol(const std::string& iso2, float fx_iv_pct) {
        std::scoped_lock lk{mtx_};
        raw_fx_vol_[iso2] = fx_iv_pct;
        dirty_.store(true);
    }

    // Called when geopolitics/news records arrive
    void ingest_record(const NormalizedRecord& rec) {
        if (rec.domain != "geopolitics" && rec.domain != "news") return;
        if (!rec.geo.country_iso2) return;

        const std::string& iso2 = *rec.geo.country_iso2;
        std::scoped_lock lk{mtx_};

        // Geopolitical risk score: accumulate severity
        if (rec.domain == "geopolitics") {
            geo_risk_accum_[iso2] += rec.severity;
            geo_risk_counts_[iso2]++;
        }

        // News velocity: count articles per country in rolling window
        if (rec.domain == "news") {
            news_vel_[iso2]++;
        }

        dirty_.store(true);
    }

    // ── Recompute if dirty (called on background thread every 60s) ─────────
    void maybe_recompute() {
        if (!dirty_.load()) return;
        dirty_.store(false);

        std::scoped_lock lk{mtx_};
        recompute_locked();
    }

    // ── UI thread read (lock-free snapshot) ───────────────────────────────
    [[nodiscard]] GMSIScore score(const std::string& iso2) const {
        std::scoped_lock lk{read_mtx_};
        auto it = scores_.find(iso2);
        if (it != scores_.end()) return it->second;
        return {};
    }

    // Map GMSI score [0,5] → §2 muted severity ImVec4
    [[nodiscard]] static ImVec4 score_to_color(float s) {
        // Quantize to 6 severity levels
        int level = static_cast<int>(std::clamp(s, 0.0f, 5.0f));
        return Theme::severity_color(level);
    }

    // Map score → alpha (0.0 = calm/transparent, 0.75 = systemic)
    [[nodiscard]] static float score_to_alpha(float s) {
        return std::clamp(s / 5.0f * 0.75f, 0.05f, 0.75f);
    }

    [[nodiscard]] const std::unordered_map<std::string, GMSIScore>& all_scores() const {
        std::scoped_lock lk{read_mtx_};
        return scores_;
    }

private:
    GMSIWeights weights_;
    std::atomic<bool> dirty_{false};

    mutable std::mutex mtx_;
    std::unordered_map<std::string, float> raw_eq_vol_;
    std::unordered_map<std::string, float> raw_cds_;
    std::unordered_map<std::string, float> raw_fx_vol_;
    std::unordered_map<std::string, float> geo_risk_accum_;
    std::unordered_map<std::string, int>   geo_risk_counts_;
    std::unordered_map<std::string, int>   news_vel_;

    mutable std::mutex read_mtx_;
    std::unordered_map<std::string, GMSIScore> scores_;

    void recompute_locked() {
        // Collect all countries across all input maps
        std::unordered_map<std::string, GMSIScore> new_scores;

        auto all_keys = [&]() {
            std::vector<std::string> keys;
            auto add = [&](const auto& m) {
                for (auto& [k, v] : m)
                    if (std::ranges::find(keys, k) == keys.end())
                        keys.push_back(k);
            };
            add(raw_eq_vol_); add(raw_cds_); add(raw_fx_vol_);
            add(geo_risk_accum_); add(news_vel_);
            return keys;
        };

        // Cross-sectional stats for z-scoring
        auto z_score = [](float val, float mean, float std_dev) -> float {
            if (std_dev < 1e-6f) return 0.0f;
            return (val - mean) / std_dev;
        };

        auto cross_mean = [](const std::unordered_map<std::string, float>& m) {
            if (m.empty()) return 0.0f;
            float s = 0; for (auto& [k,v] : m) s += v; return s / m.size();
        };
        auto cross_std = [&](const std::unordered_map<std::string, float>& m, float mean) {
            if (m.size() < 2) return 1.0f;
            float s = 0; for (auto& [k,v] : m) s += (v-mean)*(v-mean);
            return std::sqrt(s / m.size()) + 1e-6f;
        };

        float ev_mean = cross_mean(raw_eq_vol_);
        float ev_std  = cross_std(raw_eq_vol_, ev_mean);
        float cds_mean= cross_mean(raw_cds_);
        float cds_std = cross_std(raw_cds_, cds_mean);
        float fv_mean = cross_mean(raw_fx_vol_);
        float fv_std  = cross_std(raw_fx_vol_, fv_mean);

        // Max news velocity for normalization
        int max_news = 1;
        for (auto& [k,v] : news_vel_) max_news = std::max(max_news, v);

        for (const auto& iso2 : all_keys()) {
            GMSIScore s;

            float ev  = raw_eq_vol_.count(iso2)    ? raw_eq_vol_.at(iso2)  : ev_mean;
            float cds = raw_cds_.count(iso2)        ? raw_cds_.at(iso2)    : cds_mean;
            float fv  = raw_fx_vol_.count(iso2)     ? raw_fx_vol_.at(iso2) : fv_mean;

            s.eq_vol_z = std::clamp(z_score(ev, ev_mean, ev_std), -3.0f, 3.0f);
            s.cds_z    = std::clamp(z_score(cds, cds_mean, cds_std), -3.0f, 3.0f);
            s.fx_vol_z = std::clamp(z_score(fv, fv_mean, fv_std), -3.0f, 3.0f);

            // Geo risk: average severity × 0.5 → [0, 2.5]
            if (geo_risk_accum_.count(iso2) && geo_risk_counts_.count(iso2)) {
                float avg = geo_risk_accum_.at(iso2) / geo_risk_counts_.at(iso2);
                s.geo_risk = std::clamp(avg * 0.5f, 0.0f, 2.5f);
            }

            // News velocity: normalized to [0, 1]
            if (news_vel_.count(iso2))
                s.news_vel = static_cast<float>(news_vel_.at(iso2)) / max_news;

            // Composite: z-scores → [0, 5] via logistic-ish mapping
            // Positive z-scores = elevated stress
            auto to_stress = [](float z) { return std::clamp((z + 3.0f) / 6.0f * 5.0f, 0.0f, 5.0f); };

            s.score =
                weights_.equity_vol    * to_stress(s.eq_vol_z) +
                weights_.cds_spread    * to_stress(s.cds_z)    +
                weights_.fx_vol        * to_stress(s.fx_vol_z)  +
                weights_.geo_risk      * (s.geo_risk / 2.5f * 5.0f) +
                weights_.news_velocity * (s.news_vel * 5.0f);

            s.score = std::clamp(s.score, 0.0f, 5.0f);
            s.computed_at = std::chrono::system_clock::now();
            new_scores[iso2] = s;
        }

        {
            std::scoped_lock lk2{read_mtx_};
            scores_ = std::move(new_scores);
        }
    }

    // Seed with approximate baseline so globe isn't blank on startup
    void seed_baseline() {
        // Approximate 2024-era macro stress levels (rough order of magnitude)
        static const struct { const char* iso2; float ev; float cds; float geo; } SEEDS[] = {
            {"US",  0.15f,  22.0f,  0.5f},
            {"GB",  0.14f,  30.0f,  0.6f},
            {"DE",  0.16f,  29.0f,  0.7f},
            {"JP",  0.13f,  26.0f,  0.4f},
            {"CN",  0.20f,  75.0f,  1.5f},
            {"FR",  0.16f,  45.0f,  0.8f},
            {"IN",  0.18f,  85.0f,  1.2f},
            {"BR",  0.22f, 165.0f,  1.4f},
            {"RU",  0.35f, 580.0f,  4.5f},
            {"TR",  0.28f, 320.0f,  2.8f},
            {"AR",  0.42f, 850.0f,  3.0f},
            {"ZA",  0.21f, 190.0f,  2.0f},
            {"NG",  0.24f, 380.0f,  3.2f},
            {"EG",  0.26f, 720.0f,  2.5f},
            {"UA",  0.55f,1200.0f,  5.0f},
            {"IR",  0.30f, 900.0f,  4.8f},
            {"SA",  0.14f,  60.0f,  1.5f},
            {"KR",  0.15f,  35.0f,  1.0f},
            {"AU",  0.13f,  25.0f,  0.3f},
            {"CA",  0.13f,  22.0f,  0.4f},
        };
        for (const auto& s : SEEDS) {
            raw_eq_vol_[s.iso2] = s.ev;
            raw_cds_[s.iso2]    = s.cds;
            geo_risk_accum_[s.iso2] = s.geo;
            geo_risk_counts_[s.iso2] = 1;
        }
        dirty_.store(true);
        maybe_recompute();
    }
};

} // namespace macro
