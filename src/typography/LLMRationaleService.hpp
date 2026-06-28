#pragma once
// src/topography/LLMRationaleService.hpp
// ─────────────────────────────────────────────────────────────────────────────
// §7.4 LLM rationale pipeline using Anthropic Claude API.
//
// Per spec:
//   • HTTPS POST to Anthropic /v1/messages (via HttpClient/libcurl)
//   • Schema-constrained JSON response: sector, direction, conviction,
//     rationale (≤60 words), key_drivers [str, str, str]
//   • Cache keyed by (sector, region, date, hash-of-factor-snapshot)
//   • Deterministic template-based fallback — terminal never shows blank
//     rationale due to API outage
//   • All calls are async on background jthread; results marshaled to UI
//     via a results queue polled each frame
// ─────────────────────────────────────────────────────────────────────────────
#include "FactorModel.hpp"
#include "../providers/HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <format>
#include <functional>
#include <mutex>
#include <print>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

namespace macro {

// ── Schema-validated output ───────────────────────────────────────────────────
struct RegimeRationale {
    std::string sector;
    std::string region;
    std::string direction;   // "bullish" | "bearish" | "neutral"
    std::string conviction;  // "low"    | "medium"  | "high"
    std::string rationale;   // ≤ ~60 words
    std::array<std::string, 3> key_drivers;
    bool from_llm{true};     // false = deterministic fallback
};

class LLMRationaleService {
public:
    using ResultCallback = std::function<void(RegimeRationale)>;

    explicit LLMRationaleService(std::string anthropic_api_key)
        : api_key_(std::move(anthropic_api_key)) {}

    // ── Request a rationale (non-blocking; result delivered via callback) ──
    void request(const RegimeResult& result,
                 const std::string& news_digest,
                 ResultCallback cb) {
        std::string cache_key = make_cache_key(result);

        // Check cache first (UI thread safe — cache only written from worker)
        {
            std::scoped_lock lk{cache_mtx_};
            auto it = cache_.find(cache_key);
            if (it != cache_.end()) {
                cb(it->second);
                return;
            }
        }

        // Enqueue request for background worker
        {
            std::scoped_lock lk{req_mtx_};
            pending_requests_.push({result, news_digest, std::move(cb), cache_key});
        }
        dirty_.store(true, std::memory_order_release);
    }

    // ── Start background worker thread ─────────────────────────────────────
    void start() {
        worker_ = std::jthread([this](std::stop_token st) {
            run(st);
        });
    }

    void stop() {
        worker_.request_stop();
    }

    // ── Drain result callbacks (UI thread, called each frame) ──────────────
    void dispatch_pending_results() {
        std::queue<std::pair<ResultCallback, RegimeRationale>> local;
        {
            std::scoped_lock lk{res_mtx_};
            std::swap(local, pending_results_);
        }
        while (!local.empty()) {
            auto& [cb, rat] = local.front();
            cb(std::move(rat));
            local.pop();
        }
    }

    [[nodiscard]] int cache_size() const {
        std::scoped_lock lk{cache_mtx_};
        return static_cast<int>(cache_.size());
    }

private:
    struct Request {
        RegimeResult  result;
        std::string   news_digest;
        ResultCallback callback;
        std::string   cache_key;
    };

    std::string api_key_;
    HttpClient  http_;
    std::jthread worker_;
    std::atomic<bool> dirty_{false};

    mutable std::mutex cache_mtx_;
    std::unordered_map<std::string, RegimeRationale> cache_;

    std::mutex req_mtx_;
    std::queue<Request> pending_requests_;

    std::mutex res_mtx_;
    std::queue<std::pair<ResultCallback, RegimeRationale>> pending_results_;

    // ── Worker loop ────────────────────────────────────────────────────────
    void run(std::stop_token st) {
        std::println("[LLMRationale] worker started");
        while (!st.stop_requested()) {
            if (!dirty_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            Request req;
            {
                std::scoped_lock lk{req_mtx_};
                if (pending_requests_.empty()) { dirty_.store(false); continue; }
                req = std::move(pending_requests_.front());
                pending_requests_.pop();
                if (pending_requests_.empty()) dirty_.store(false);
            }

            auto rat = call_llm(req.result, req.news_digest);

            // Cache it
            {
                std::scoped_lock lk{cache_mtx_};
                cache_[req.cache_key] = rat;
            }

            // Enqueue for UI dispatch
            {
                std::scoped_lock lk{res_mtx_};
                pending_results_.push({std::move(req.callback), std::move(rat)});
            }
        }
        std::println("[LLMRationale] worker stopped");
    }

    // ── Anthropic API call ─────────────────────────────────────────────────
    RegimeRationale call_llm(const RegimeResult& r, const std::string& news_digest) {
        if (api_key_.empty()) return deterministic_fallback(r);

        // Build factor summary string
        std::string factor_summary;
        static constexpr const char* FACTOR_NAMES[] = {
            "Real Rates", "Inflation Surprise", "Growth Proxy",
            "USD Index", "Credit Spread", "Oil"
        };
        for (int i = 0; i < N_FACTORS; ++i) {
            factor_summary += std::format("{}: beta={:.3f} contrib={:.3f}; ",
                FACTOR_NAMES[i], r.factor_betas[i], r.factor_contributions[i]);
        }

        std::string user_prompt = std::format(
            "Sector: {} | Region: {}\n"
            "Regime Score: {:.3f} | Conviction: {:.2f} | R²: {:.3f} | Breadth: {:.1f}%\n"
            "Factor exposures: {}\n"
            "Relevant headlines: {}\n\n"
            "Return ONLY valid JSON matching exactly this schema "
            "(no markdown, no preamble):\n"
            "{{\"sector\":string,\"direction\":\"bullish\"|\"bearish\"|\"neutral\","
            "\"conviction\":\"low\"|\"medium\"|\"high\","
            "\"rationale\":string (max 60 words),"
            "\"key_drivers\":[string,string,string]}}",
            r.sector, r.region,
            r.regime_score, r.conviction, r.r_squared, r.breadth * 100.0f,
            factor_summary,
            news_digest.empty() ? "none" : news_digest.substr(0, 400));

        nlohmann::json body = {
            {"model",      "claude-sonnet-4-6"},
            {"max_tokens", 300},
            {"system",     "You are a quantitative macro analyst. "
                           "Return only schema-valid JSON per the user's specification. "
                           "Rationale must be ≤60 words. No markdown fences."},
            {"messages",   {{{"role", "user"}, {"content", user_prompt}}}}
        };

        auto resp = http_.post(
            "https://api.anthropic.com/v1/messages",
            body.dump(),
            {{"x-api-key",         api_key_},
             {"anthropic-version",  "2023-06-01"}});

        if (!resp) {
            std::println("[LLMRationale] API call failed: {}", resp.error().message);
            return deterministic_fallback(r);
        }

        try {
            auto resp_j = nlohmann::json::parse(resp->body);
            std::string text;
            // Extract text content from response
            for (auto& block : resp_j.value("content", nlohmann::json::array())) {
                if (block.value("type", "") == "text") {
                    text = block.value("text", "");
                    break;
                }
            }

            // Strip any accidental markdown fences
            auto strip = [](std::string s) {
                if (s.starts_with("```")) {
                    auto end = s.rfind("```");
                    if (end != std::string::npos && end > 3)
                        s = s.substr(s.find('\n') + 1, end - s.find('\n') - 1);
                }
                return s;
            };
            text = strip(text);

            auto j = nlohmann::json::parse(text);
            return parse_rationale(j, r, true);

        } catch (const std::exception& e) {
            std::println("[LLMRationale] parse error: {}", e.what());
            return deterministic_fallback(r);
        }
    }

    // ── Schema parser ──────────────────────────────────────────────────────
    static RegimeRationale parse_rationale(const nlohmann::json& j,
                                           const RegimeResult& r,
                                           bool from_llm) {
        RegimeRationale rat;
        rat.sector    = j.value("sector",    std::string(r.sector));
        rat.region    = j.value("region",    std::string(r.region));
        rat.direction = j.value("direction", "neutral");
        rat.conviction = j.value("conviction","low");
        rat.rationale  = j.value("rationale", "");
        rat.from_llm   = from_llm;

        // Truncate rationale to 60 words
        std::string& rt = rat.rationale;
        int wc = 0;
        std::size_t pos = 0;
        for (std::size_t i = 0; i < rt.size(); ++i) {
            if (rt[i] == ' ' || rt[i] == '\n') { ++wc; pos = i; }
            if (wc >= 60) { rt = rt.substr(0, pos); break; }
        }

        // key_drivers
        auto& kd = j.value("key_drivers", nlohmann::json::array());
        for (int i = 0; i < 3 && i < static_cast<int>(kd.size()); ++i)
            rat.key_drivers[i] = kd[i].get<std::string>();

        // Validate direction/conviction enums
        if (rat.direction != "bullish" && rat.direction != "bearish")
            rat.direction = "neutral";
        if (rat.conviction != "medium" && rat.conviction != "high")
            rat.conviction = "low";

        return rat;
    }

    // ── Deterministic fallback — always produces a valid rationale ─────────
    static RegimeRationale deterministic_fallback(const RegimeResult& r) {
        RegimeRationale rat;
        rat.sector    = std::string(r.sector);
        rat.region    = std::string(r.region);
        rat.from_llm  = false;

        // Direction from regime score
        if (r.regime_score > 0.3f)       rat.direction = "bullish";
        else if (r.regime_score < -0.3f) rat.direction = "bearish";
        else                              rat.direction = "neutral";

        // Conviction
        if (r.conviction > 0.65f)        rat.conviction = "high";
        else if (r.conviction > 0.35f)   rat.conviction = "medium";
        else                             rat.conviction = "low";

        // Find top 3 factor contributors by absolute value
        std::array<std::pair<float,int>, N_FACTORS> sorted_contribs;
        for (int i = 0; i < N_FACTORS; ++i)
            sorted_contribs[i] = {std::abs(r.factor_contributions[i]), i};
        std::ranges::sort(sorted_contribs, std::ranges::greater{}, &std::pair<float,int>::first);

        static constexpr const char* FACTOR_NAMES[] = {
            "real rates", "inflation surprise", "growth proxy",
            "USD index", "credit spreads", "oil prices"
        };

        for (int i = 0; i < 3; ++i)
            rat.key_drivers[i] = FACTOR_NAMES[sorted_contribs[i].second];

        rat.rationale = std::format(
            "{} {} regime with {} conviction. "
            "Score {:.2f}. R²={:.2f}. "
            "Dominant drivers: {}, {}, {}.",
            r.region, r.sector, rat.conviction,
            r.regime_score, r.r_squared,
            rat.key_drivers[0], rat.key_drivers[1], rat.key_drivers[2]);

        return rat;
    }

    // ── Cache key ──────────────────────────────────────────────────────────
    static std::string make_cache_key(const RegimeResult& r) {
        // Include date + a coarse quantization of the score to avoid
        // redundant calls when factor snapshot barely changes
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        std::tm gmt{};
#ifdef _WIN32
        gmtime_s(&gmt, &tt);
#else
        gmtime_r(&tt, &gmt);
#endif
        char date[12];
        std::strftime(date, sizeof(date), "%Y-%m-%d", &gmt);

        int score_bucket = static_cast<int>(r.regime_score * 10);
        int conv_bucket  = static_cast<int>(r.conviction   * 5);
        return std::format("{}:{}:{}:{}:{}", r.sector, r.region, date,
                           score_bucket, conv_bucket);
    }
};

} // namespace macro
