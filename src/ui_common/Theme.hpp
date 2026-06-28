#pragma once
// src/ui_common/Theme.hpp — §2 institutional palette
#include <imgui.h>

namespace macro::Theme {

// ── Backgrounds ───────────────────────────────────────────────────────────
constexpr ImVec4 BG_PRIMARY   {0.039f,0.055f,0.078f,1.0f};
constexpr ImVec4 BG_SECONDARY {0.051f,0.067f,0.090f,1.0f};
constexpr ImVec4 BG_PANEL     {0.063f,0.082f,0.110f,1.0f};
constexpr ImVec4 BG_ELEVATED  {0.075f,0.098f,0.130f,1.0f};

// ── Borders ───────────────────────────────────────────────────────────────
constexpr ImVec4 BORDER_SUBTLE {0.18f,0.22f,0.28f,0.45f};
constexpr ImVec4 BORDER_ACTIVE {0.18f,0.83f,1.00f,0.30f};

// ── Accents ───────────────────────────────────────────────────────────────
constexpr ImVec4 ACCENT_CYAN      {0.18f,0.83f,1.00f,1.0f};
constexpr ImVec4 ACCENT_CYAN_DIM  {0.18f,0.83f,1.00f,0.20f};
constexpr ImVec4 ACCENT_BLUE_SOLID{0.23f,0.49f,0.65f,1.0f};

// ── Severity (muted / desaturated) ───────────────────────────────────────
constexpr ImVec4 SEV_INFORMATIONAL{0.55f,0.62f,0.70f,1.0f};
constexpr ImVec4 SEV_LOW          {0.45f,0.58f,0.52f,1.0f};
constexpr ImVec4 SEV_ELEVATED     {0.70f,0.68f,0.42f,1.0f};
constexpr ImVec4 SEV_HIGH         {0.78f,0.52f,0.32f,1.0f};
constexpr ImVec4 SEV_CRITICAL     {0.71f,0.35f,0.29f,1.0f};
constexpr ImVec4 SEV_SYSTEMIC     {0.82f,0.22f,0.18f,1.0f};

// ── Directional ───────────────────────────────────────────────────────────
constexpr ImVec4 DIR_BULLISH{0.30f,0.49f,0.47f,1.0f};
constexpr ImVec4 DIR_BEARISH{0.71f,0.35f,0.29f,1.0f};
constexpr ImVec4 DIR_NEUTRAL{0.50f,0.55f,0.60f,1.0f};

// ── Text ──────────────────────────────────────────────────────────────────
constexpr ImVec4 TEXT_PRIMARY  {0.86f,0.88f,0.90f,1.0f};
constexpr ImVec4 TEXT_SECONDARY{0.55f,0.60f,0.65f,1.0f};
constexpr ImVec4 TEXT_MUTED    {0.35f,0.38f,0.42f,1.0f};
constexpr ImVec4 TEXT_STALE    {0.71f,0.35f,0.29f,1.0f};

// ── Conviction ────────────────────────────────────────────────────────────
constexpr ImVec4 CONVICTION_HIGH  = DIR_BULLISH;
constexpr ImVec4 CONVICTION_MEDIUM= SEV_ELEVATED;
constexpr ImVec4 CONVICTION_LOW   = SEV_INFORMATIONAL;

// ── Geometry ──────────────────────────────────────────────────────────────
constexpr float PADDING_OUTER       = 12.0f;
constexpr float PADDING_INNER       = 8.0f;
constexpr float ROUNDING_PANEL      = 4.0f;
constexpr float ROUNDING_CHIP       = 3.0f;
constexpr float BORDER_WIDTH        = 1.0f;
constexpr float STATUS_BAR_HEIGHT   = 24.0f;
constexpr float FILTER_RAIL_WIDTH   = 220.0f;
constexpr float CONTEXT_PANEL_WIDTH = 300.0f;

[[nodiscard]] inline ImU32 to_u32(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

void apply_style();

[[nodiscard]] const ImVec4& severity_color(int level);
[[nodiscard]] const ImVec4& direction_color(int dir);

} // namespace macro::Theme
