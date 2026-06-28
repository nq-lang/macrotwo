#pragma once
// src/providers/HttpClient.hpp
// Synchronous libcurl HTTP client returning std::expected.
// All network calls happen on provider jthreads — never on the UI thread.
#include "IDataProvider.hpp"
#include <curl/curl.h>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace macro {

struct HttpResponse {
    int         status_code{0};
    std::string body;
};

class HttpClient {
public:
    HttpClient()  { curl_global_init(CURL_GLOBAL_DEFAULT); handle_ = curl_easy_init(); }
    ~HttpClient() { if (handle_) curl_easy_cleanup(handle_); }

    HttpClient(const HttpClient&)            = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    using Headers = std::vector<std::pair<std::string, std::string>>;

    [[nodiscard]] std::expected<HttpResponse, ProviderError>
    get(std::string_view url, const Headers& extra = {}) {
        return request(url, {}, extra);
    }

    [[nodiscard]] std::expected<HttpResponse, ProviderError>
    post(std::string_view url, std::string_view body, const Headers& extra = {}) {
        return request(url, body, extra);
    }

private:
    CURL* handle_{nullptr};

    static std::size_t write_cb(char* ptr, std::size_t sz, std::size_t nm, void* ud) {
        static_cast<std::string*>(ud)->append(ptr, sz * nm);
        return sz * nm;
    }

    [[nodiscard]] std::expected<HttpResponse, ProviderError>
    request(std::string_view url, std::string_view body, const Headers& extra) {
        if (!handle_)
            return std::unexpected(ProviderError{ProviderErrorKind::Unavailable, "no curl handle"});

        curl_easy_reset(handle_);
        HttpResponse resp;
        std::string url_str{url};

        curl_easy_setopt(handle_, CURLOPT_URL,           url_str.c_str());
        curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(handle_, CURLOPT_WRITEDATA,     &resp.body);
        curl_easy_setopt(handle_, CURLOPT_TIMEOUT,       30L);
        curl_easy_setopt(handle_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle_, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle_, CURLOPT_USERAGENT,     "MacroTerminal/0.3");

        curl_slist* hdrs = nullptr;
        if (!body.empty()) {
            hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
            std::string bstr{body};
            curl_easy_setopt(handle_, CURLOPT_POSTFIELDS, bstr.c_str());
        }
        for (auto& [k, v] : extra) {
            hdrs = curl_slist_append(hdrs, (k + ": " + v).c_str());
        }
        if (hdrs) curl_easy_setopt(handle_, CURLOPT_HTTPHEADER, hdrs);

        CURLcode res = curl_easy_perform(handle_);
        if (hdrs) curl_slist_free_all(hdrs);

        if (res != CURLE_OK)
            return std::unexpected(ProviderError{
                ProviderErrorKind::NetworkFailure, curl_easy_strerror(res)});

        long code = 0;
        curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &code);
        resp.status_code = static_cast<int>(code);

        if (code == 401 || code == 403)
            return std::unexpected(ProviderError{ProviderErrorKind::AuthFailure,
                "HTTP " + std::to_string(code), static_cast<int>(code)});
        if (code == 429)
            return std::unexpected(ProviderError{ProviderErrorKind::RateLimitExceeded,
                "Rate limited", 429});
        if (code >= 500)
            return std::unexpected(ProviderError{ProviderErrorKind::Unavailable,
                "HTTP " + std::to_string(code), static_cast<int>(code)});

        return resp;
    }
};

} // namespace macro
