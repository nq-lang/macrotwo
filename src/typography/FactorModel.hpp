#pragma once
// src/topography/FactorModel.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Quantitative factor model (§7.3):
//   • Runs multi-factor OLS regression of sector returns vs macro factors
//   • Computes Regime Score = weighted z-score blend of (beta × current-factor-move)
//   • Computes Conviction = f(R², constituent-level dispersion)
//
// Uses Eigen3 for all linear algebra. TerrainGrid uses C++23 std::mdspan.
// ─────────────────────────────────────────────────────────────────────────────
#include <Eigen/Dense>
#include <array>
#include <mdspan>       // C++23
#include <numeric>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace macro {

// ── Factor index constants ───────────────────────────────────────────────────
enum class MacroFactor : int {
    RealRates      = 0,   // Δ10Y real yield (TIPS)
    InflationSurp  = 1,   // CPI surprise vs consensus
    GrowthProxy    = 2,   // ISM-style diffusion index
    USDIndex       = 3,   // DXY or trade-weighted USD
    CreditSpread   = 4,   // HY − IG OAS spread
    OilPrice       = 5,   // WTI or Brent front-month
    COUNT          = 6
};

static constexpr int N_FACTORS = static_cast<int>(MacroFactor::COUNT);

// ── GICS sector labels ───────────────────────────────────────────────────────
static constexpr std::array<std::string_view, 11> GICS_SECTORS = {
    "Energy", "Materials", "Industrials", "Cons.Disc", "Cons.Staples",
    "Healthcare", "Financials", "InfoTech", "CommSvcs", "Utilities", "RealEstate"
};
static constexpr int N_SECTORS = 11;

// ── Regime result per sector/region ─────────────────────────────────────────
struct RegimeResult {
    std::string_view sector;
    std::string_view region;         // "US" | "Europe"
    float            regime_score;   // positive=bullish, negative=bearish
    float            conviction;     // 0..1
    float            r_squared;      // regression fit quality
    float            breadth;        // % constituents above 50-day MA
    std::array<float, N_FACTORS> factor_betas{};
    std::array<float, N_FACTORS> factor_contributions{}; // beta × current factor move
};

// ── TerrainGrid using C++23 std::mdspan with multidim operator[] ─────────────
class TerrainGrid {
public:
    TerrainGrid(std::size_t rows, std::size_t cols)
        : storage_(rows * cols, 0.0f)
        , view_(storage_.data(), rows, cols) {}

    // C++23 multidimensional operator[]
    float& operator[](std::size_t r, std::size_t c) { return view_[r, c]; }
    float  operator[](std::size_t r, std::size_t c) const { return view_[r, c]; }

    auto span() const { return view_; }
    std::size_t rows() const { return view_.extent(0); }
    std::size_t cols() const { return view_.extent(1); }

private:
    std::vector<float>                                    storage_;
    std::mdspan<float, std::dextents<std::size_t, 2>>    view_;
};

// ── FactorModel ──────────────────────────────────────────────────────────────
class FactorModel {
public:
    struct Config {
        // Factor weights for composite regime score z-blend
        std::array<float, N_FACTORS> factor_weights = {
            0.25f,  // RealRates      — dominant for rate-sensitive sectors
            0.20f,  // InflationSurp
            0.20f,  // GrowthProxy
            0.15f,  // USDIndex
            0.10f,  // CreditSpread
            0.10f,  // OilPrice
        };
        float breadth_weight{0.20f}; // blend weight for momentum/breadth term
        int   lookback_days{252};    // regression window
    };

    explicit FactorModel(Config cfg = {}) : cfg_(std::move(cfg)) {}

    /// Update current macro factor levels.
    /// Called by data providers when new readings arrive.
    void set_factor(MacroFactor f, float value) {
        current_factors_[static_cast<int>(f)] = value;
    }

    /// Update sector return history for regression.
    /// rows = time, cols = sectors; call with rolling window.
    void update_returns(const Eigen::MatrixXf& sector_returns,
                        const Eigen::MatrixXf& factor_returns) {
        sector_returns_ = sector_returns;
        factor_returns_ = factor_returns;
        dirty_          = true;
    }

    /// Recompute all regime results.
    /// Returns false if insufficient data.
    bool recompute() {
        if (sector_returns_.rows() < 20 || factor_returns_.rows() < 20) {
            std::println("[FactorModel] insufficient data — using stub scores");
            fill_stub_results();
            return false;
        }

        results_us_.clear();
        results_eu_.clear();

        for (int s = 0; s < N_SECTORS; ++s) {
            auto res_us = regress_sector(s, "US");
            auto res_eu = regress_sector(s, "Europe");
            results_us_.push_back(res_us);
            results_eu_.push_back(res_eu);
        }

        dirty_ = false;
        return true;
    }

    const std::vector<RegimeResult>& results_us() const { return results_us_; }
    const std::vector<RegimeResult>& results_eu() const { return results_eu_; }

    /// Build heightfield terrain from results.
    /// Grid: rows = sectors (11), cols = fine subdivisions for mesh smoothness.
    TerrainGrid build_terrain(const std::vector<RegimeResult>& results,
                              int mesh_cols = 32) const {
        TerrainGrid grid(N_SECTORS, static_cast<std::size_t>(mesh_cols));

        for (int s = 0; s < N_SECTORS; ++s) {
            const auto& r = results[static_cast<std::size_t>(s)];
            // Height = regime_score; jaggedness from 1/conviction
            float amplitude  = r.regime_score;
            float jaggedness = 1.0f - r.conviction; // low conviction → flat

            for (int c = 0; c < mesh_cols; ++c) {
                // Add controlled noise scaled by jaggedness
                float noise = jaggedness * 0.2f * std::sin(
                    c * 0.8f + static_cast<float>(s) * 1.7f);
                grid[static_cast<std::size_t>(s),
                     static_cast<std::size_t>(c)] = amplitude + noise;
            }
        }
        return grid;
    }

    bool is_dirty() const { return dirty_; }

private:
    Config cfg_;
    std::array<float, N_FACTORS> current_factors_{};
    Eigen::MatrixXf sector_returns_;
    Eigen::MatrixXf factor_returns_;
    std::vector<RegimeResult> results_us_;
    std::vector<RegimeResult> results_eu_;
    bool dirty_{true};

    RegimeResult regress_sector(int sector_idx, std::string_view region) {
        // OLS: y = X * beta  where y = sector returns, X = factor returns
        int T = static_cast<int>(sector_returns_.rows());
        if (T < 20) return make_stub_result(sector_idx, region);

        Eigen::VectorXf y = sector_returns_.col(sector_idx).head(T);
        Eigen::MatrixXf X(T, N_FACTORS + 1);
        X.col(0) = Eigen::VectorXf::Ones(T); // intercept
        for (int f = 0; f < N_FACTORS && f < factor_returns_.cols(); ++f)
            X.col(f + 1) = factor_returns_.col(f).head(T);

        // Normal equations: beta = (X'X)^-1 X'y
        Eigen::VectorXf beta = (X.transpose() * X).ldlt().solve(X.transpose() * y);

        // R² computation
        float y_bar = y.mean();
        float ss_tot = (y.array() - y_bar).square().sum();
        float ss_res = (y - X * beta).array().square().sum();
        float r2     = (ss_tot > 1e-8f) ? (1.0f - ss_res / ss_tot) : 0.0f;

        // Regime score: sum of (beta_i × current_factor_i) with weights
        float regime_score = 0.0f;
        RegimeResult res;
        res.sector   = GICS_SECTORS[static_cast<std::size_t>(sector_idx)];
        res.region   = region;
        res.r_squared = std::clamp(r2, 0.0f, 1.0f);

        for (int f = 0; f < N_FACTORS; ++f) {
            float b       = (f + 1 < static_cast<int>(beta.size())) ? beta(f + 1) : 0.0f;
            float contrib = b * current_factors_[f] * cfg_.factor_weights[f];
            res.factor_betas[f]        = b;
            res.factor_contributions[f] = contrib;
            regime_score              += contrib;
        }

        // Breadth stub (would come from constituent data in Phase 4)
        float breadth = 0.5f;
        res.breadth = breadth;

        // Composite regime score with breadth term
        regime_score = (1.0f - cfg_.breadth_weight) * regime_score
                     + cfg_.breadth_weight * (breadth - 0.5f) * 2.0f;

        // Conviction = r² × (1 − constituent dispersion placeholder)
        res.conviction = std::clamp(r2 * 0.8f + 0.1f, 0.0f, 1.0f);
        res.regime_score = std::clamp(regime_score, -3.0f, 3.0f);

        return res;
    }

    void fill_stub_results() {
        // Deterministic stubs so the terrain shows something before real data
        static constexpr float stub_scores_us[] = {
            0.8f, -0.3f, 0.5f, -1.2f, 0.4f,
            0.1f, -0.7f, 1.5f, 0.6f, -0.2f, -0.9f
        };
        static constexpr float stub_scores_eu[] = {
            0.3f, 0.7f, -0.4f, -0.8f, 0.6f,
            0.2f, 0.9f, 0.4f, -0.1f, 0.5f, 0.2f
        };

        results_us_.clear();
        results_eu_.clear();
        for (int s = 0; s < N_SECTORS; ++s) {
            results_us_.push_back(make_stub_result_with_score(s, "US",  stub_scores_us[s]));
            results_eu_.push_back(make_stub_result_with_score(s, "Europe", stub_scores_eu[s]));
        }
    }

    static RegimeResult make_stub_result(int s, std::string_view region) {
        return make_stub_result_with_score(s, region, 0.0f);
    }

    static RegimeResult make_stub_result_with_score(int s, std::string_view region, float score) {
        RegimeResult r;
        r.sector        = GICS_SECTORS[static_cast<std::size_t>(s)];
        r.region        = region;
        r.regime_score  = score;
        r.conviction    = 0.4f;
        r.r_squared     = 0.3f;
        r.breadth       = 0.5f;
        return r;
    }
};

} // namespace macro
