// src/main.cpp
// ─────────────────────────────────────────────────────────────────────────────
// Macro Intelligence Terminal — entry point.
// Loads secrets from environment, starts the Application, runs the render loop.
// ─────────────────────────────────────────────────────────────────────────────
#include "app/Application.hpp"
#include "app/Secrets.hpp"
#include <cstdlib>
#include <print>

// Optional: load a .env file from the current working directory into env vars.
// This is a small utility — not a dependency; we implement it inline so we
// never add dotenv as a vcpkg dep.
static void try_load_dotenv(const char* path = ".env") {
    std::FILE* f = std::fopen(path, "r");
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        // Strip trailing newline
        for (char* p = line; *p; ++p) {
            if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
        }
        // Skip comments and blank lines
        if (line[0] == '#' || line[0] == '\0') continue;
        // Expect KEY=VALUE
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;
        // Only set if not already in environment (env var takes precedence)
        if (!std::getenv(key)) {
#ifdef _WIN32
            _putenv_s(key, val);
#else
            ::setenv(key, val, 0);
#endif
        }
    }
    std::fclose(f);
    std::println("[main] .env loaded from {}", path);
}

int main(int argc, char** argv) {
    std::println("═══════════════════════════════════════════════════════");
    std::println("  MACRO INTELLIGENCE TERMINAL  v0.1  (C++23)");
    std::println("  Internal Use Only — Read-Only Analytics");
    std::println("═══════════════════════════════════════════════════════");

    // Optional --fullscreen / --env flags
    macro::Application::Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if (arg == "--fullscreen")  cfg.fullscreen     = true;
        if (arg == "--env" && i+1 < argc) try_load_dotenv(argv[++i]);
    }

    // Default: try loading .env from working directory
    try_load_dotenv(".env");

    // Load secrets from environment
    auto secrets_result = macro::load_secrets();
    if (!secrets_result) {
        auto& err = secrets_result.error();
        std::println("[main] ── MISSING REQUIRED API KEYS ──────────────────");
        for (auto& v : err.missing_vars)
            std::println("[main]   export {}=<your-key>", v);
        std::println("[main] ─────────────────────────────────────────────────");
        std::println("[main] Terminal will start in degraded mode (providers disabled).");
        // Create empty secrets so UI still launches
        macro::Secrets empty_sec;
        auto app_result = macro::Application::create(empty_sec, cfg);
        if (!app_result) {
            std::println("[main] FATAL: {}", app_result.error().message);
            return EXIT_FAILURE;
        }
        app_result->run();
        return EXIT_SUCCESS;
    }

    auto app_result = macro::Application::create(*secrets_result, cfg);
    if (!app_result) {
        std::println("[main] FATAL: {}", app_result.error().message);
        return EXIT_FAILURE;
    }

    std::println("[main] Application initialized — entering render loop");
    app_result->run();
    std::println("[main] Clean shutdown complete");
    return EXIT_SUCCESS;
}
