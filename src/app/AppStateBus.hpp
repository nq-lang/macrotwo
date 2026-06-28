#pragma once
// src/app/AppStateBus.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Thread-safe publish/subscribe bus for GeoSelectionContext.
//
// Publishing thread: Layer 1 (Globe) background drill-down state machine.
// Subscribing threads: all UI-side consumers (tables, topography, panels).
//
// Contract:
//   • publish() marshals the context to an internal queue.
//   • dispatch_pending() is called once per frame on the main (UI) thread
//     and drains the queue, calling all registered subscriber callbacks.
//     This guarantees callbacks always run on the UI thread — no cross-thread
//     ImGui mutation hazard.
//   • subscribe() / unsubscribe() may be called from any thread.
// ─────────────────────────────────────────────────────────────────────────────
#include "GeoSelectionContext.hpp"

#include <atomic>
#include <flat_map>      // C++23
#include <functional>
#include <mutex>
#include <queue>
#include <cstddef>

namespace macro {

class AppStateBus {
public:
    using Token = std::size_t;
    using Callback = std::function<void(const GeoSelectionContext&)>;

    // ── Subscription management (any thread) ─────────────────────────────
    Token subscribe(Callback cb) {
        std::scoped_lock lk{sub_mtx_};
        Token tok = next_token_++;
        subscribers_.emplace(tok, std::move(cb));
        return tok;
    }

    void unsubscribe(Token tok) {
        std::scoped_lock lk{sub_mtx_};
        subscribers_.erase(tok);
    }

    // ── Publishing (any thread — typically background worker) ─────────────
    void publish(const GeoSelectionContext& ctx) {
        {
            std::scoped_lock lk{queue_mtx_};
            pending_.push(ctx);
        }
        dirty_.store(true, std::memory_order_release);
    }

    // ── Drain (UI thread only — called once per frame) ───────────────────
    /// Returns the number of contexts dispatched this frame.
    std::size_t dispatch_pending() {
        if (!dirty_.load(std::memory_order_acquire))
            return 0;

        // Swap out the pending queue under lock, then dispatch without holding it.
        std::queue<GeoSelectionContext> local_queue;
        {
            std::scoped_lock lk{queue_mtx_};
            std::swap(local_queue, pending_);
            dirty_.store(false, std::memory_order_release);
        }

        // Take a snapshot of subscriber callbacks (may change during iteration).
        std::vector<Callback> cbs;
        {
            std::scoped_lock lk{sub_mtx_};
            cbs.reserve(subscribers_.size());
            for (auto& [tok, cb] : subscribers_)
                cbs.push_back(cb);
        }

        std::size_t count = 0;
        while (!local_queue.empty()) {
            const GeoSelectionContext& ctx = local_queue.front();
            for (auto& cb : cbs)
                cb(ctx);
            local_queue.pop();
            ++count;
        }
        return count;
    }

    // ── Read current state (UI thread) ────────────────────────────────────
    const GeoSelectionContext& current() const noexcept { return current_; }

private:
    // C++23 std::flat_map: cache-friendly sorted key-value storage
    std::flat_map<Token, Callback> subscribers_;
    std::mutex sub_mtx_;

    std::queue<GeoSelectionContext> pending_;
    std::mutex queue_mtx_;
    std::atomic<bool> dirty_{false};

    GeoSelectionContext current_;
    std::atomic<Token> next_token_{1};
};

} // namespace macro
