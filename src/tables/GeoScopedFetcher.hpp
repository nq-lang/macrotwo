#pragma once
// src/tables/GeoScopedFetcher.hpp
// ─────────────────────────────────────────────────────────────────────────────
// On-demand geo-scoped article fetcher for Section 2.
//
// Design
//   Single background std::jthread.  The UI thread deposits a
//   GeoSelectionContext via request_fetch() (already debounced by TablesLayer).
//   The worker picks it up, fires all 7 domain fetches sequentially (safe for
//   a single HTTP handle), and pushes FetchResult objects into a result queue.
//   Results are drained by TablesLayer::tick() on the UI thread each frame
//   — no ImGui calls happen off the main thread.
//
// Rate limiting
//   Each API is tracked in last_fetch_ map.  A minimum interval (per-API)
//   is enforced.  If the interval has not elapsed the API call is skipped;
//   previously cached articles remain visible.
//
// Fetch routing by zoom tier
//   Tier 0 (World)     — global headlines from NewsAPI, GNews, Finnhub
//   Tier 1 (Continent) — continent-scoped query, WorldNewsAPI + NewsAPI q=
//   Tier 2 (Country)   — NewsAPI country filter + FRED releases + GNews
//   Tier 3 (Local)     — WorldNewsAPI + NewsData.io + GNews city search
//   Tier 4 (Ground)    — suppressed (satellite pane owns this tier)
// ─────────────────────────────────────────────────────────────────────────────
#include "ArticleRecord.hpp"
#include "../app/GeoSelectionContext.hpp"
#include "../app/Secrets.hpp"
#include "../providers/HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <print>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace macro {

struct FetchResult {
    FeedDomain             domain{FeedDomain::MacroDevelopments};
    std::vector<ArticleRecord> articles;
    std::string            fetch_tier_label;
    bool is_loading{false};
    bool is_error  {false};
    std::string error_msg;
};

class GeoScopedFetcher {
public:
    explicit GeoScopedFetcher(const Secrets& sec) : sec_(sec) {}
    ~GeoScopedFetcher() { stop(); }

    void start() {
        running_ = true;
        worker_  = std::jthread([this](std::stop_token st){ run(st); });
    }
    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.request_stop();
    }

    /// UI thread → worker: enqueue a new fetch context (supersedes any pending)
    void request_fetch(const GeoSelectionContext& ctx) {
        std::scoped_lock lk{req_mtx_};
        pending_ = ctx;
        has_pending_.store(true, std::memory_order_release);
    }

    /// UI thread: drain completed FetchResult objects
    std::vector<FetchResult> drain_results() {
        std::vector<FetchResult> out;
        std::scoped_lock lk{res_mtx_};
        while (!results_.empty()) {
            out.push_back(std::move(results_.front()));
            results_.pop();
        }
        return out;
    }

    [[nodiscard]] bool is_fetching() const noexcept {
        return busy_.load(std::memory_order_relaxed);
    }

private:
    Secrets     sec_;
    std::jthread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> has_pending_{false};
    std::atomic<bool> busy_{false};

    std::mutex                         req_mtx_;
    std::optional<GeoSelectionContext> pending_;

    std::mutex            res_mtx_;
    std::queue<FetchResult> results_;

    // Per-API minimum interval tracking
    std::mutex last_fetch_mtx_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_fetch_;

    // ── Worker ────────────────────────────────────────────────────────────
    void run(std::stop_token st) {
        std::println("[GeoFetcher] started");
        while (!st.stop_requested() && running_.load()) {
            if (!has_pending_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds{40});
                continue;
            }
            GeoSelectionContext ctx;
            {
                std::scoped_lock lk{req_mtx_};
                if (!pending_) { has_pending_ = false; continue; }
                ctx = *pending_;
                pending_.reset();
                has_pending_.store(false, std::memory_order_release);
            }

            const int tier = fetch_tier(ctx.resolution);
            if (tier == 4) continue;          // satellite — nothing to do
            busy_ = true;

            // Push loading sentinels
            std::string tl = tier_lbl(tier);
            for (int d = 0; d < FEED_DOMAIN_COUNT; ++d)
                push({static_cast<FeedDomain>(d), {}, tl, true, false, ""});

            // Sequential domain fetches
            using FD = FeedDomain;
            for (auto fd : {FD::MacroDevelopments, FD::MicroDevelopments,
                            FD::GeopoliticalTensions, FD::CentralBankUpdates,
                            FD::MonetaryPolicy, FD::GlobalRegionalNews,
                            FD::MilitaryWarNews})
            {
                if (st.stop_requested()) break;
                fetch_domain(fd, ctx, tier, tl);
            }
            busy_ = false;
        }
        std::println("[GeoFetcher] stopped");
    }

    void fetch_domain(FeedDomain fd, const GeoSelectionContext& ctx,
                      int tier, const std::string& tl) {
        FetchResult r; r.domain = fd; r.fetch_tier_label = tl;
        try {
            r.articles = dispatch(fd, ctx, tier);
        } catch (const std::exception& e) {
            r.is_error = true; r.error_msg = e.what();
        }
        push(std::move(r));
    }

    std::vector<ArticleRecord> dispatch(FeedDomain fd,
                                        const GeoSelectionContext& ctx,
                                        int tier) {
        using FD = FeedDomain;
        const std::string q  = scope_query(ctx, topic_terms(fd));
        const int         mx = 10;
        switch (fd) {
            case FD::MacroDevelopments:
                return newsapi(q, ctx, fd, tier, mx)
                     + gnews  (q, ctx, fd, tier, 6)
                     + finnhub("economic", ctx, fd, tier, 4);
            case FD::MicroDevelopments:
                return newsapi(q, ctx, fd, tier, mx)
                     + polygon(ctx, fd, tier, 6);
            case FD::GeopoliticalTensions:
                return newsapi  (q, ctx, fd, tier, mx)
                     + worldnews(q, ctx, fd, tier, 8);
            case FD::CentralBankUpdates:
                return newsapi(q, ctx, fd, tier, mx)
                     + finnhub("forex", ctx, fd, tier, 4);
            case FD::MonetaryPolicy: {
                auto v = newsapi(q, ctx, fd, tier, mx);
                if (tier <= 2) {
                    auto f = fred_releases(ctx, fd);
                    v.insert(v.end(), f.begin(), f.end());
                }
                return v;
            }
            case FD::GlobalRegionalNews:
                return newsapi  (q, ctx, fd, tier, mx)
                     + gnews    (q, ctx, fd, tier, 6)
                     + newsdata (q, ctx, fd, tier, 6);
            case FD::MilitaryWarNews:
                return worldnews(q, ctx, fd, tier, mx)
                     + newsapi  (q, ctx, fd, tier, 8);
        }
        return {};
    }

    // ── Query builders ────────────────────────────────────────────────────
    static std::string topic_terms(FeedDomain fd) {
        switch (fd) {
            case FeedDomain::MacroDevelopments:
                return "GDP OR inflation OR CPI OR employment OR NFP OR "
                       "economic growth OR recession OR central bank";
            case FeedDomain::MicroDevelopments:
                return "corporate earnings OR company revenue OR sector news OR "
                       "consumer spending OR retail sales OR business results";
            case FeedDomain::GeopoliticalTensions:
                return "geopolitics OR trade war OR sanctions OR diplomatic OR "
                       "treaty OR election OR political crisis OR coup";
            case FeedDomain::CentralBankUpdates:
                return "Federal Reserve OR ECB OR Bank of England OR BOJ OR "
                       "PBOC OR rate decision OR monetary policy OR balance sheet";
            case FeedDomain::MonetaryPolicy:
                return "interest rate hike OR rate cut OR quantitative easing OR "
                       "forward guidance OR basis points OR QE OR QT";
            case FeedDomain::GlobalRegionalNews:
                return "supply chain OR commodity prices OR energy prices OR "
                       "trade deficit OR economic outlook OR housing market";
            case FeedDomain::MilitaryWarNews:
                return "military conflict OR war OR armed forces OR defense "
                       "spending OR NATO OR ceasefire OR airstrike OR troops";
        }
        return "economy";
    }

    static std::string scope_query(const GeoSelectionContext& ctx,
                                   const std::string& topic) {
        if (ctx.resolution == GeoResolution::World) return topic;
        std::string geo = ctx.to_query_string();
        return "(" + topic + ") AND (" + geo + ")";
    }

    // ── Rate limiting ─────────────────────────────────────────────────────
    bool rate_ok(const std::string& key,
                 std::chrono::seconds min_secs = std::chrono::seconds{45}) {
        std::scoped_lock lk{last_fetch_mtx_};
        auto& t = last_fetch_[key];
        if (std::chrono::steady_clock::now() - t < min_secs) return false;
        t = std::chrono::steady_clock::now();
        return true;
    }

    // ── URL encode (minimal) ──────────────────────────────────────────────
    static std::string enc(const std::string& s) {
        std::string o; o.reserve(s.size() * 2);
        for (unsigned char c : s) {
            if (std::isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~') o += char(c);
            else if (c==' ') o += "%20";
            else { char b[4]; std::snprintf(b,4,"%%%02X",c); o += b; }
        }
        return o;
    }

    // ── ArticleRecord factory ─────────────────────────────────────────────
    static ArticleRecord make(FeedDomain fd, const GeoSelectionContext& ctx,
                              int tier,
                              std::string src, std::string url,
                              std::string headline, std::string snippet,
                              std::string id, int sev,
                              std::chrono::system_clock::time_point ts) {
        ArticleRecord a;
        a.domain          = fd;
        a.source_name     = std::move(src);
        a.source_url      = std::move(url);
        a.headline        = std::move(headline);
        a.snippet         = std::move(snippet);
        a.id              = std::move(id);
        a.geo_label       = ctx.selected_name();
        a.fetch_tier_label= tier_lbl(tier);
        a.is_geo_fetched  = true;
        a.severity        = sev;
        a.published_at    = ts;
        a.metric_tags     = extract_pills(a.headline + " " + a.snippet);
        return a;
    }

    // ── NewsAPI.org ───────────────────────────────────────────────────────
    std::vector<ArticleRecord> newsapi(const std::string& q,
                                       const GeoSelectionContext& ctx,
                                       FeedDomain fd, int tier, int mx) {
        if (sec_.newsapi_api_key.empty()) return {};
        if (!rate_ok("newsapi")) return {};

        HttpClient h;
        std::string country = ctx.to_api_geo_param();
        std::string url;
        if (!country.empty() && tier == 2)
            url = std::format(
                "https://newsapi.org/v2/top-headlines?category=business"
                "{}&pageSize={}&apiKey={}",
                country, mx, sec_.newsapi_api_key);
        else
            url = std::format(
                "https://newsapi.org/v2/everything?q={}&language=en"
                "&sortBy=publishedAt&pageSize={}&apiKey={}",
                enc(q.substr(0,450)), mx, sec_.newsapi_api_key);

        auto r = h.get(url);
        if (!r || r->status_code != 200) return {};

        std::vector<ArticleRecord> out;
        try {
            auto j = nlohmann::json::parse(r->body);
            if (j.value("status","") != "ok") return out;
            for (auto& a : j.value("articles", nlohmann::json::array())) {
                std::string src = a.contains("source")
                    ? a["source"].value("name","NewsAPI") : "NewsAPI";
                std::string hl  = a.value("title","");
                if (hl.empty()) continue;
                std::string snip = a.value("description","");
                if (snip.empty())
                    snip = a.value("content","").substr(
                        0, std::min(a.value("content","").size(), std::size_t{240}));
                out.push_back(make(fd, ctx, tier,
                    src, a.value("url",""), hl, snip,
                    std::to_string(std::hash<std::string>{}(a.value("url",""))),
                    classify(hl), parse_ts(a.value("publishedAt",""))));
            }
        } catch (...) {}
        return out;
    }

    // ── GNews ─────────────────────────────────────────────────────────────
    std::vector<ArticleRecord> gnews(const std::string& q,
                                     const GeoSelectionContext& ctx,
                                     FeedDomain fd, int tier, int mx) {
        if (sec_.gnews_api_key.empty()) return {};
        if (!rate_ok("gnews", std::chrono::seconds{60})) return {};

        HttpClient h;
        auto r = h.get(std::format(
            "https://gnews.io/api/v4/search?q={}&lang=en&max={}&token={}",
            enc(q.substr(0,280)), mx, sec_.gnews_api_key));
        if (!r || r->status_code != 200) return {};

        std::vector<ArticleRecord> out;
        try {
            auto j = nlohmann::json::parse(r->body);
            for (auto& a : j.value("articles", nlohmann::json::array())) {
                std::string src = a.contains("source")
                    ? a["source"].value("name","GNews") : "GNews";
                std::string hl  = a.value("title","");
                if (hl.empty()) continue;
                out.push_back(make(fd, ctx, tier,
                    src, a.value("url",""), hl, a.value("description",""),
                    std::to_string(std::hash<std::string>{}(a.value("url",""))),
                    classify(hl), parse_ts(a.value("publishedAt",""))));
            }
        } catch (...) {}
        return out;
    }

    // ── WorldNewsAPI ──────────────────────────────────────────────────────
    std::vector<ArticleRecord> worldnews(const std::string& q,
                                         const GeoSelectionContext& ctx,
                                         FeedDomain fd, int tier, int mx) {
        if (sec_.worldnewsapi_api_key.empty()) return {};
        if (!rate_ok("worldnews", std::chrono::seconds{60})) return {};

        HttpClient h;
        auto r = h.get(
            std::format("https://api.worldnewsapi.com/search-news?"
                        "text={}&number={}&api-key={}",
                        enc(q.substr(0,280)), mx, sec_.worldnewsapi_api_key),
            {{"x-api-key", sec_.worldnewsapi_api_key}});
        if (!r || r->status_code != 200) return {};

        std::vector<ArticleRecord> out;
        try {
            auto j = nlohmann::json::parse(r->body);
            for (auto& a : j.value("news", nlohmann::json::array())) {
                std::string hl = a.value("title","");
                if (hl.empty()) continue;
                std::string snip = a.value("text","");
                if (snip.size() > 260) snip = snip.substr(0,260) + "…";
                out.push_back(make(fd, ctx, tier,
                    "WorldNewsAPI", a.value("url",""), hl, snip,
                    std::to_string(a.value("id",0)),
                    classify(hl), parse_ts(a.value("publish_date",""))));
            }
        } catch (...) {}
        return out;
    }

    // ── NewsData.io ───────────────────────────────────────────────────────
    std::vector<ArticleRecord> newsdata(const std::string& q,
                                        const GeoSelectionContext& ctx,
                                        FeedDomain fd, int tier, int mx) {
        if (sec_.newsdataio_api_key.empty()) return {};
        if (!rate_ok("newsdata", std::chrono::seconds{90})) return {};

        HttpClient h;
        std::string cp;
        if (tier == 2 && !ctx.country_iso2.empty()) {
            std::string lc = ctx.country_iso2;
            std::ranges::transform(lc, lc.begin(), ::tolower);
            cp = "&country=" + lc;
        }
        auto r = h.get(std::format(
            "https://newsdata.io/api/1/news?q={}&language=en"
            "&category=business,politics{}&apikey={}",
            enc(q.substr(0,180)), cp, sec_.newsdataio_api_key));
        if (!r || r->status_code != 200) return {};

        std::vector<ArticleRecord> out;
        int cnt = 0;
        try {
            auto j = nlohmann::json::parse(r->body);
            for (auto& a : j.value("results", nlohmann::json::array())) {
                if (cnt++ >= mx) break;
                std::string hl = a.value("title","");
                if (hl.empty()) continue;
                out.push_back(make(fd, ctx, tier,
                    a.value("source_id","NewsData"),
                    a.value("link",""), hl, a.value("description",""),
                    a.value("article_id",""),
                    classify(hl), parse_ts(a.value("pubDate",""))));
            }
        } catch (...) {}
        return out;
    }

    // ── Finnhub news ──────────────────────────────────────────────────────
    std::vector<ArticleRecord> finnhub(const std::string& cat,
                                       const GeoSelectionContext& ctx,
                                       FeedDomain fd, int tier, int mx) {
        if (sec_.finnhub_api_key.empty()) return {};
        if (!rate_ok("finnhub_news", std::chrono::seconds{60})) return {};

        HttpClient h;
        auto r = h.get(std::format(
            "https://finnhub.io/api/v1/news?category={}&token={}",
            cat, sec_.finnhub_api_key));
        if (!r || r->status_code != 200) return {};

        std::vector<ArticleRecord> out;
        int cnt = 0;
        try {
            auto j = nlohmann::json::parse(r->body);
            for (auto& a : j) {
                if (cnt++ >= mx) break;
                std::string hl = a.value("headline","");
                if (hl.empty()) continue;
                std::string snip = a.value("summary","");
                if (snip.size() > 260) snip = snip.substr(0,260)+"…";
                out.push_back(make(fd, ctx, tier,
                    a.value("source","Finnhub"), a.value("url",""),
                    hl, snip, std::to_string(a.value("id",0)),
                    classify(hl),
                    std::chrono::system_clock::from_time_t(
                        static_cast<std::time_t>(a.value("datetime",0LL)))));
            }
        } catch (...) {}
        return out;
    }

    // ── Polygon.io news ───────────────────────────────────────────────────
    std::vector<ArticleRecord> polygon(const GeoSelectionContext& ctx,
                                       FeedDomain fd, int tier, int mx) {
        if (sec_.polygon_api_key.empty()) return {};
        if (!rate_ok("polygon_news", std::chrono::seconds{60})) return {};

        HttpClient h;
        auto r = h.get(std::format(
            "https://api.polygon.io/v2/reference/news?"
            "limit={}&order=desc&apiKey={}",
            mx, sec_.polygon_api_key));
        if (!r || r->status_code != 200) return {};

        std::vector<ArticleRecord> out;
        try {
            auto j = nlohmann::json::parse(r->body);
            for (auto& a : j.value("results", nlohmann::json::array())) {
                std::string hl = a.value("title","");
                if (hl.empty()) continue;
                out.push_back(make(fd, ctx, tier,
                    "Polygon.io", a.value("article_url",""),
                    hl, a.value("description",""), a.value("id",""),
                    classify(hl), parse_ts(a.value("published_utc",""))));
            }
        } catch (...) {}
        return out;
    }

    // ── FRED releases ─────────────────────────────────────────────────────
    std::vector<ArticleRecord> fred_releases(const GeoSelectionContext& ctx,
                                             FeedDomain fd) {
        if (sec_.fred_api_key.empty()) return {};
        if (!rate_ok("fred_rel", std::chrono::seconds{300})) return {};

        HttpClient h;
        auto r = h.get(std::format(
            "https://api.stlouisfed.org/fred/releases?"
            "api_key={}&file_type=json&limit=8&sort_order=desc",
            sec_.fred_api_key));
        if (!r || r->status_code != 200) return {};

        std::vector<ArticleRecord> out;
        try {
            auto j = nlohmann::json::parse(r->body);
            for (auto& rel : j.value("releases", nlohmann::json::array())) {
                std::string name = rel.value("name","");
                if (name.empty()) continue;
                ArticleRecord a;
                a.domain           = fd;
                a.source_name      = "FRED / St. Louis Fed";
                a.headline         = "FRED Release: " + name;
                a.snippet          = rel.value("notes","").substr(
                    0, std::min(rel.value("notes","").size(), std::size_t{280}));
                a.id               = "fred:rel:" + std::to_string(rel.value("id",0));
                a.geo_label        = ctx.selected_name();
                a.fetch_tier_label = "COUNTRY";
                a.is_geo_fetched   = true;
                a.severity         = 1;
                a.published_at     = std::chrono::system_clock::now();
                out.push_back(std::move(a));
            }
        } catch (...) {}
        return out;
    }

    // ── Classification helpers ────────────────────────────────────────────
    static int classify(const std::string& txt) {
        std::string lo = txt;
        std::ranges::transform(lo, lo.begin(), ::tolower);
        if (lo.contains("war") || lo.contains("invasion")   ||
            lo.contains("collapse") || lo.contains("default"))  return 5;
        if (lo.contains("military") || lo.contains("conflict") ||
            lo.contains("sanctions") || lo.contains("coup"))    return 4;
        if (lo.contains("hike") || lo.contains("rate decision")||
            lo.contains("inflation") || lo.contains("recession"))return 3;
        if (lo.contains("gdp") || lo.contains("employment")    ||
            lo.contains("central bank") || lo.contains("fed"))  return 2;
        if (lo.contains("market") || lo.contains("economy"))    return 1;
        return 0;
    }

    static std::vector<MetricTag> extract_pills(const std::string& text) {
        struct Pat { const char* kw; const char* lbl; };
        static constexpr Pat PATS[] = {
            {"basis points","bps"},{"bps","bps"},
            {"inflation","CPI"},{"cpi","CPI"},{"rate hike","Rate"},
            {"rate cut","Rate"},{"gdp","GDP"},{"unemployment","UNR"},
            {"nfp","NFP"},{"non-farm","NFP"},{"pmi","PMI"},{"yield","YLD"},
        };
        std::string lo = text;
        std::ranges::transform(lo, lo.begin(), ::tolower);
        std::vector<MetricTag> tags;
        for (const auto& p : PATS) {
            if (tags.size() >= 3) break;
            auto pos = lo.find(p.kw);
            if (pos == std::string::npos) continue;
            // Find nearby number
            std::size_t start = pos > 20 ? pos - 20 : 0;
            std::string win   = lo.substr(start, 40);
            for (std::size_t i = 0; i < win.size(); ++i) {
                if (!std::isdigit((unsigned char)win[i])) continue;
                std::size_t j = i;
                while (j < win.size() && (std::isdigit((unsigned char)win[j])
                       || win[j]=='.' || win[j]=='%' || win[j]=='-'
                       || win[j]=='+')) ++j;
                std::string val = win.substr(i, j-i);
                if (j < win.size() && win[j] == '%') val += '%';
                if (!val.empty()) {
                    MetricTag t; t.label=p.lbl; t.value=val; t.severity=1;
                    tags.push_back(std::move(t));
                }
                break;
            }
        }
        return tags;
    }

    static std::chrono::system_clock::time_point parse_ts(const std::string& s) {
        if (s.empty()) return std::chrono::system_clock::now();
        int y=2024,mo=1,d=1,h=0,mi=0,sc=0;
        std::sscanf(s.c_str(),"%d-%d-%dT%d:%d:%d",&y,&mo,&d,&h,&mi,&sc);
        std::tm tm{};
        tm.tm_year=y-1900; tm.tm_mon=mo-1; tm.tm_mday=d;
        tm.tm_hour=h; tm.tm_min=mi; tm.tm_sec=sc;
        auto t = std::mktime(&tm);
        return t != -1 ? std::chrono::system_clock::from_time_t(t)
                       : std::chrono::system_clock::now();
    }

    static std::string tier_lbl(int t) noexcept {
        switch(t){case 0:return "GLOBAL";case 1:return "CONTINENT";
                  case 2:return "COUNTRY";case 3:return "LOCAL";
                  default:return "SATELLITE";}
    }

    void push(FetchResult r) {
        std::scoped_lock lk{res_mtx_};
        results_.push(std::move(r));
    }

    // Vector concat helper
    static std::vector<ArticleRecord> operator+(
            std::vector<ArticleRecord> a, std::vector<ArticleRecord>&& b) {
        a.insert(a.end(),
            std::make_move_iterator(b.begin()),
            std::make_move_iterator(b.end()));
        return a;
    }
};

} // namespace macro
