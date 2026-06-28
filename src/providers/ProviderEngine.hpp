#pragma once
// src/providers/ProviderEngine.hpp
// Manages all IDataProvider instances on dedicated std::jthreads.
// Results pushed through MPSC queue to UI thread.
// UI thread calls drain() once per frame — never blocks on network.
#include "IDataProvider.hpp"
#include "../app/Secrets.hpp"

// All providers
#include "FredProvider.hpp"
#include "FinnhubProvider.hpp"
#include "PolygonProvider.hpp"
#include "NewsAggregatorProvider.hpp"
#include "AlphaVantageProvider.hpp"
#include "MarketStackProvider.hpp"
#include "TradierProvider.hpp"
#include "AxionQuantProvider.hpp"
#include "IMFProvider.hpp"
#include "WorldBankProvider.hpp"
#include "OpenMeteoProvider.hpp"
#include "WHOProvider.hpp"
#include "NASAGIBSProvider.hpp"
#include "GEEProvider.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <print>
#include <queue>
#include <thread>
#include <vector>

namespace macro {

// Thread-safe MPSC record queue
class RecordQueue {
public:
    void push(NormalizedRecord rec) {
        std::scoped_lock lk{mtx_};
        queue_.push(std::move(rec));
    }

    // UI thread: drain up to max_records per frame
    std::vector<NormalizedRecord> drain(int max_records = 300) {
        std::vector<NormalizedRecord> out;
        out.reserve(static_cast<std::size_t>(max_records));
        std::scoped_lock lk{mtx_};
        while (!queue_.empty() && static_cast<int>(out.size()) < max_records) {
            out.push_back(std::move(queue_.front()));
            queue_.pop();
        }
        return out;
    }

    [[nodiscard]] std::size_t size() const {
        std::scoped_lock lk{mtx_};
        return queue_.size();
    }

private:
    mutable std::mutex mtx_;
    std::queue<NormalizedRecord> queue_;
};

class ProviderEngine {
public:
    explicit ProviderEngine(const Secrets& sec, RecordQueue& queue)
        : queue_(queue) {
        // ── Market data ────────────────────────────────────────────────────
        add<FredProvider>(sec.fred_api_key);
        add<FinnhubProvider>(sec.finnhub_api_key);
        add<PolygonProvider>(sec.polygon_api_key);
        add<AlphaVantageProvider>(sec.alpha_vantage_api_key);
        add<MarketStackProvider>(sec.marketstack_api_key);
        add<TradierProvider>(sec.tradier_api_key);
        add<AxionQuantProvider>(sec.axionquant_api_key);

        // ── News ───────────────────────────────────────────────────────────
        add<NewsAggregatorProvider>(NewsAggregatorProvider::Config{
            sec.newsapi_api_key,
            sec.gnews_api_key,
            sec.newsdataio_api_key,
            sec.worldnewsapi_api_key});

        // ── Free / institutional APIs ──────────────────────────────────────
        add<IMFProvider>();
        add<WorldBankProvider>();
        add<OpenMeteoProvider>();
        add<WHOProvider>();

        // ── Geospatial / remote sensing ────────────────────────────────────
        add<NASAGIBSProvider>(sec.nasa_gibs_api_key);
        add<GEEProvider>(GEEProvider::Config{
            sec.gee_project_id,
            sec.gee_service_account_json});

        std::println("[ProviderEngine] {} providers registered", providers_.size());
    }

    void start() {
        for (auto& prov : providers_) {
            IDataProvider* raw = prov.get();
            workers_.emplace_back([this, raw](std::stop_token st) {
                run_provider(raw, st);
            });
        }
        std::println("[ProviderEngine] all workers started");
    }

    void stop() {
        for (auto& w : workers_) w.request_stop();
        std::println("[ProviderEngine] stop requested for all workers");
    }

    [[nodiscard]] int total()   const { return static_cast<int>(providers_.size()); }
    [[nodiscard]] int healthy() const { return healthy_.load(); }
    [[nodiscard]] int failed()  const { return failed_.load();  }

private:
    RecordQueue& queue_;
    std::vector<std::unique_ptr<IDataProvider>> providers_;
    std::vector<std::jthread>                   workers_;
    std::atomic<int>                            healthy_{0};
    std::atomic<int>                            failed_{0};

    // Helper: construct a provider and register it
    template<typename T, typename... Args>
    void add(Args&&... args) {
        providers_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    }

    void run_provider(IDataProvider* prov, std::stop_token st) {
        std::println("[{}] connecting...", prov->name());
        auto conn = prov->connect();
        if (!conn) {
            std::println("[{}] FAILED: {}", prov->name(), conn.error().message);
            failed_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        healthy_.fetch_add(1, std::memory_order_relaxed);

        while (!st.stop_requested()) {
            try {
                for (auto rec : prov->poll()) {
                    if (st.stop_requested()) break;
                    queue_.push(std::move(rec));
                }
            } catch (const std::exception& e) {
                std::println("[{}] poll exception: {}", prov->name(), e.what());
            }

            // Sleep for poll_interval, checking stop token every 200ms
            auto deadline = std::chrono::steady_clock::now() + prov->poll_interval();
            while (!st.stop_requested() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        std::println("[{}] stopped cleanly", prov->name());
    }
};

} // namespace macro
