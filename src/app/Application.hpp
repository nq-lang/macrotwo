#pragma once
// src/app/Application.hpp — v0.3 (Section 2 remodel)
// ─────────────────────────────────────────────────────────────────────────────
// Main application shell.
//
// SECTION 1 (37 %): Globe — left overlay rail, centre osgEarth FBO, right
//                   location-context / alert-stream panel.
// SECTION 2 (34 %): Event-driven geo-scoped intelligence feed (7 stacked
//                   narrative modules; 2-second debounce on viewport changes).
// SECTION 3 (29 %): 3-D sector-regime topography + conviction tables.
//
// Key change from v0.2:
//   • TablesLayer now receives the Secrets reference so GeoScopedFetcher can
//     make live API calls when the globe viewport settles.
//   • tables_->tick() is called inside Application::tick() so the debounce
//     timer and fetch-result drain happen every frame, before render().
// ─────────────────────────────────────────────────────────────────────────────
#include "AppStateBus.hpp"
#include "GeoSelectionContext.hpp"
#include "Secrets.hpp"
#include "../globe/GlobeLayer.hpp"
#include "../tables/TablesLayer.hpp"
#include "../topography/TopographyLayer.hpp"
#include "../ui_common/Theme.hpp"
#include "../ui_common/StatusBar.hpp"
#include "../ui_common/FilterRail.hpp"
#include "../ui_common/LocationContextPanel.hpp"
#include "../providers/ProviderEngine.hpp"

#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <chrono>
#include <expected>
#include <memory>
#include <print>
#include <string>

namespace macro {

struct AppError { std::string message; };

class Application {
public:
    struct Config {
        int         window_width {1920};
        int         window_height{1080};
        const char* window_title {
            "MACRO INTELLIGENCE TERMINAL  //  INTERNAL USE ONLY — UNCLASSIFIED"};
        bool        fullscreen{false};
        // Vertical proportions of the centre column (must sum ≤ 1.0)
        float s1_ratio{0.37f};
        float s2_ratio{0.34f};
        float s3_ratio{0.29f};
    };

    [[nodiscard]] static std::expected<Application, AppError>
    create(const Secrets& sec, Config cfg = {}) {
        Application app(sec, cfg);
        if (auto e = app.init(); !e) return std::unexpected(e.error());
        return app;
    }

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&)                 = default;
    Application& operator=(Application&&)      = default;
    ~Application() { shutdown(); }

    void run() {
        engine_->start();
        while (!glfwWindowShouldClose(window_)) {
            glfwPollEvents();
            tick();
            render_frame();
            glfwSwapBuffers(window_);
        }
        engine_->stop();
    }

private:
    Config       cfg_;
    GLFWwindow*  window_{nullptr};
    AppStateBus  bus_;
    SourceHealth source_health_;
    RecordQueue  record_queue_;
    Secrets      sec_;

    std::unique_ptr<GlobeLayer>           globe_;
    std::unique_ptr<TablesLayer>          tables_;
    std::unique_ptr<TopographyLayer>      topo_;
    std::unique_ptr<FilterRail>           filter_rail_;
    std::unique_ptr<LocationContextPanel> context_panel_;
    std::unique_ptr<StatusBar>            status_bar_;
    std::unique_ptr<ProviderEngine>       engine_;

    AppStateBus::Token ctx_token_{};

    Application(const Secrets& sec, Config cfg) : cfg_(cfg), sec_(sec) {}

    std::expected<void, AppError> init() {
        // ── GLFW + OpenGL 4.6 core ────────────────────────────────────────
        if (!glfwInit())
            return std::unexpected(AppError{"glfwInit() failed"});

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, 4);

        GLFWmonitor* mon = cfg_.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
        window_ = glfwCreateWindow(cfg_.window_width, cfg_.window_height,
                                   cfg_.window_title, mon, nullptr);
        if (!window_) {
            glfwTerminate();
            return std::unexpected(AppError{"glfwCreateWindow() failed"});
        }
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
            return std::unexpected(AppError{"gladLoadGLLoader() failed"});

        glEnable(GL_MULTISAMPLE);
        std::println("[App] OpenGL {} / {}",
            reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

        // ── Dear ImGui ────────────────────────────────────────────────────
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename  = "macro_terminal_layout.ini";

        Theme::apply_style();
        load_fonts(io);

        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        // ── Layers ────────────────────────────────────────────────────────
        filter_rail_   = std::make_unique<FilterRail>();
        context_panel_ = std::make_unique<LocationContextPanel>();
        status_bar_    = std::make_unique<StatusBar>(source_health_);
        globe_         = std::make_unique<GlobeLayer>(bus_);
        // TablesLayer now takes Secrets so GeoScopedFetcher can call APIs
        tables_        = std::make_unique<TablesLayer>(bus_, sec_);
        topo_          = std::make_unique<TopographyLayer>(bus_, sec_.anthropic_api_key);

        ctx_token_ = bus_.subscribe([this](const GeoSelectionContext& ctx) {
            context_panel_->update_context(ctx);
        });

        engine_ = std::make_unique<ProviderEngine>(sec_, record_queue_);

        std::println("[App] initialisation complete — entering render loop");
        return {};
    }

    // ── Per-frame logic ───────────────────────────────────────────────────
    void tick() {
        // 1. Drain bus — delivers GeoSelectionContext to all subscribers
        bus_.dispatch_pending();

        // 2. Section 2 debounce + fetch-result drain (must run every frame)
        tables_->tick();

        // 3. Drain provider records and distribute
        auto recs = record_queue_.drain(400);
        for (auto& rec : recs) {
            tables_->ingest(rec);
            topo_->ingest(rec);
            context_panel_->ingest_record(rec);
            globe_->ingest_record(rec);
        }

        // 4. Update status-bar health counters (every 2 s)
        static auto last_hlt = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - last_hlt > std::chrono::seconds{2}) {
            source_health_.total.store(engine_->total());
            source_health_.ok   .store(engine_->healthy());
            source_health_.err  .store(engine_->failed());
            last_hlt = now;
        }
    }

    // ── Render ────────────────────────────────────────────────────────────
    void render_frame() {
        int win_w, win_h;
        glfwGetFramebufferSize(window_, &win_w, &win_h);
        glViewport(0, 0, win_w, win_h);
        glClearColor(0.039f, 0.055f, 0.078f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-screen passthrough dockspace host
        {
            constexpr ImGuiWindowFlags HF =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs  |
                ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove    |
                ImGuiWindowFlags_NoBringToDisplayFrontOnFocus              |
                ImGuiWindowFlags_NoSavedSettings;
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
            ImGui::Begin("##Host", nullptr, HF);
            ImGui::DockSpace(ImGui::GetID("##Dock"), {0, 0},
                ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::End();
            ImGui::PopStyleVar();
        }

        const float fw     = static_cast<float>(win_w);
        const float fh     = static_cast<float>(win_h);
        const float bar_h  = Theme::STATUS_BAR_HEIGHT;
        const float avail  = fh - bar_h;
        const float rail_w = filter_rail_->visible_width();
        const float ctx_w  = context_panel_->visible_width();
        const float ctr_w  = fw - rail_w - ctx_w;
        const float s1_h   = avail * cfg_.s1_ratio;
        const float s2_h   = avail * cfg_.s2_ratio;
        const float s3_h   = avail * cfg_.s3_ratio;

        // ── Section 1 — Globe ─────────────────────────────────────────────
        {
            constexpr ImGuiWindowFlags GF =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings;
            ImGui::SetNextWindowPos({rail_w, 0.0f});
            ImGui::SetNextWindowSize({ctr_w, s1_h});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::BG_PRIMARY);
            ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, {0, 0});
            if (ImGui::Begin("##S1", nullptr, GF))
                globe_->render(ctr_w, s1_h);
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        // ── Section 2 — Geo-scoped narrative feed ─────────────────────────
        tables_->render(rail_w, s1_h, ctr_w, s2_h);

        // ── Section 3 — Topography ────────────────────────────────────────
        topo_->render(rail_w, s1_h + s2_h, ctr_w, s3_h);

        // ── Panels ────────────────────────────────────────────────────────
        filter_rail_->render(fh);
        context_panel_->render(fh);
        status_bar_->render(fw);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void load_fonts(ImGuiIO& io) {
        bool ok = false;
        for (const char* p : {"data/fonts/JetBrainsMono-Regular.ttf",
                               "data/fonts/IBMPlexMono-Regular.ttf"})
            if (io.Fonts->AddFontFromFileTTF(p, 13.0f)) { ok = true; break; }
        if (!ok) io.Fonts->AddFontDefault();
        for (const char* p : {"data/fonts/IBMPlexSansCondensed-Regular.ttf",
                               "data/fonts/Inter-Regular.ttf"})
            if (io.Fonts->AddFontFromFileTTF(p, 11.0f)) break;
        io.Fonts->Build();
    }

    void shutdown() {
        if (ctx_token_) bus_.unsubscribe(ctx_token_);
        if (window_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            glfwDestroyWindow(window_);
            glfwTerminate();
        }
    }
};

} // namespace macro
