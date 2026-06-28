#pragma once
// src/providers/IDataProvider.hpp
// Common interface and NormalizedRecord schema for all 14 data providers.
#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

// C++23 std::generator — if not available yet, forward-declare stub
#if __has_include(<generator>)
#  include <generator>
#else
// Fallback: define a minimal synchronous generator concept for older toolchains.
// Remove this block once GCC-13 / Clang-17 ships std::generator fully.
namespace std {
template<typename T>
struct generator {
    struct promise_type { auto get_return_object(){return generator{};} auto initial_suspend(){return suspend_always{};} auto final_suspend()noexcept{return suspend_always{};} void return_void(){} void unhandled_exception(){throw;} auto yield_value(T){return suspend_always{};} };
    bool empty() const { return true; }
    struct iterator { bool operator!=(const iterator&) const { return false; } void operator++(){} T operator*() const { return T{}; } };
    iterator begin() const { return {}; }
    iterator end()   const { return {}; }
};
}
#endif

namespace macro {

struct GeoTag {
    std::optional<std::string> continent;
    std::optional<std::string> country_iso2;
    std::optional<std::string> country_iso3;
    std::optional<std::string> admin1;
    std::optional<std::string> city;
    std::optional<double>      lat;
    std::optional<double>      lon;

    [[nodiscard]] bool matches_country(std::string_view iso2) const noexcept {
        return country_iso2 && *country_iso2 == iso2;
    }
};

struct NormalizedRecord {
    std::string   record_id;
    std::string   domain;
    std::string   source_name;
    std::string   headline;
    std::string   payload_json;
    GeoTag        geo;
    int           severity{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

enum class ProviderErrorKind {
    NetworkFailure, AuthFailure, ParseError, RateLimitExceeded, Unavailable, Unknown
};

struct ProviderError {
    ProviderErrorKind kind{ProviderErrorKind::Unknown};
    std::string       message;
    int               http_status{0};
};

class IDataProvider {
public:
    virtual ~IDataProvider() = default;

    virtual std::expected<void, ProviderError> connect() = 0;
    virtual std::generator<NormalizedRecord>   poll()    = 0;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::chrono::seconds poll_interval() const noexcept {
        return std::chrono::seconds{60};
    }
};

} // namespace macro
