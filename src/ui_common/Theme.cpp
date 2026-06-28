// src/ui_common/Theme.cpp
#include "Theme.hpp"
#include <imgui.h>
#include <array>

namespace macro::Theme {

void apply_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]            = BG_PRIMARY;
    c[ImGuiCol_ChildBg]             = BG_PANEL;
    c[ImGuiCol_PopupBg]             = BG_SECONDARY;
    c[ImGuiCol_Border]              = BORDER_SUBTLE;
    c[ImGuiCol_BorderShadow]        = {0,0,0,0};
    c[ImGuiCol_FrameBg]             = BG_ELEVATED;
    c[ImGuiCol_FrameBgHovered]      = {0.10f,0.14f,0.18f,1.0f};
    c[ImGuiCol_FrameBgActive]       = {0.13f,0.18f,0.24f,1.0f};
    c[ImGuiCol_TitleBg]             = BG_SECONDARY;
    c[ImGuiCol_TitleBgActive]       = {0.08f,0.11f,0.15f,1.0f};
    c[ImGuiCol_TitleBgCollapsed]    = BG_PRIMARY;
    c[ImGuiCol_MenuBarBg]           = BG_SECONDARY;
    c[ImGuiCol_ScrollbarBg]         = BG_PRIMARY;
    c[ImGuiCol_ScrollbarGrab]       = BORDER_SUBTLE;
    c[ImGuiCol_ScrollbarGrabHovered]= {0.25f,0.30f,0.36f,1.0f};
    c[ImGuiCol_ScrollbarGrabActive] = ACCENT_BLUE_SOLID;
    c[ImGuiCol_CheckMark]           = ACCENT_CYAN;
    c[ImGuiCol_SliderGrab]          = ACCENT_BLUE_SOLID;
    c[ImGuiCol_SliderGrabActive]    = ACCENT_CYAN;
    c[ImGuiCol_Button]              = BG_ELEVATED;
    c[ImGuiCol_ButtonHovered]       = {0.15f,0.21f,0.28f,1.0f};
    c[ImGuiCol_ButtonActive]        = ACCENT_BLUE_SOLID;
    c[ImGuiCol_Header]              = {0.12f,0.17f,0.22f,1.0f};
    c[ImGuiCol_HeaderHovered]       = {0.16f,0.22f,0.29f,1.0f};
    c[ImGuiCol_HeaderActive]        = ACCENT_BLUE_SOLID;
    c[ImGuiCol_Separator]           = BORDER_SUBTLE;
    c[ImGuiCol_SeparatorHovered]    = ACCENT_CYAN_DIM;
    c[ImGuiCol_SeparatorActive]     = ACCENT_CYAN;
    c[ImGuiCol_Tab]                 = BG_SECONDARY;
    c[ImGuiCol_TabHovered]          = {0.14f,0.19f,0.25f,1.0f};
    c[ImGuiCol_TabActive]           = BG_ELEVATED;
    c[ImGuiCol_TabUnfocused]        = BG_SECONDARY;
    c[ImGuiCol_TabUnfocusedActive]  = BG_PANEL;
    c[ImGuiCol_DockingPreview]      = ACCENT_CYAN_DIM;
    c[ImGuiCol_DockingEmptyBg]      = BG_PRIMARY;
    c[ImGuiCol_TableHeaderBg]       = BG_SECONDARY;
    c[ImGuiCol_TableBorderLight]    = BORDER_SUBTLE;
    c[ImGuiCol_TableBorderStrong]   = {0.22f,0.27f,0.33f,0.80f};
    c[ImGuiCol_TableRowBg]          = {0,0,0,0};
    c[ImGuiCol_TableRowBgAlt]       = {0.06f,0.08f,0.11f,0.50f};
    c[ImGuiCol_TextSelectedBg]      = ACCENT_CYAN_DIM;
    c[ImGuiCol_DragDropTarget]      = ACCENT_CYAN;
    c[ImGuiCol_NavHighlight]        = ACCENT_CYAN;
    c[ImGuiCol_Text]                = TEXT_PRIMARY;
    c[ImGuiCol_TextDisabled]        = TEXT_MUTED;
    c[ImGuiCol_PlotLines]           = ACCENT_CYAN;
    c[ImGuiCol_PlotLinesHovered]    = TEXT_PRIMARY;
    c[ImGuiCol_PlotHistogram]       = ACCENT_BLUE_SOLID;
    c[ImGuiCol_PlotHistogramHovered]= ACCENT_CYAN;
    c[ImGuiCol_ResizeGrip]          = {0,0,0,0};
    c[ImGuiCol_ResizeGripHovered]   = ACCENT_CYAN_DIM;
    c[ImGuiCol_ResizeGripActive]    = ACCENT_CYAN;

    s.WindowRounding    = ROUNDING_PANEL;
    s.ChildRounding     = ROUNDING_PANEL;
    s.FrameRounding     = ROUNDING_CHIP;
    s.PopupRounding     = ROUNDING_PANEL;
    s.ScrollbarRounding = 2.0f;
    s.GrabRounding      = 2.0f;
    s.TabRounding       = 2.0f;
    s.WindowBorderSize  = BORDER_WIDTH;
    s.ChildBorderSize   = BORDER_WIDTH;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = BORDER_WIDTH;
    s.WindowPadding     = {PADDING_OUTER, PADDING_OUTER};
    s.FramePadding      = {PADDING_INNER, PADDING_INNER/2.0f};
    s.ItemSpacing       = {8.0f, 4.0f};
    s.ItemInnerSpacing  = {4.0f, 4.0f};
    s.CellPadding       = {PADDING_INNER, 3.0f};
    s.IndentSpacing     = 16.0f;
    s.ScrollbarSize     = 10.0f;
    s.GrabMinSize       = 6.0f;
}

const ImVec4& severity_color(int level) {
    static constexpr std::array<ImVec4,6> pal = {
        SEV_INFORMATIONAL, SEV_LOW, SEV_ELEVATED,
        SEV_HIGH, SEV_CRITICAL, SEV_SYSTEMIC
    };
    if (level < 0) return pal[0];
    if (level > 5) return pal[5];
    return pal[static_cast<std::size_t>(level)];
}

const ImVec4& direction_color(int dir) {
    if (dir > 0) return DIR_BULLISH;
    if (dir < 0) return DIR_BEARISH;
    return DIR_NEUTRAL;
}

} // namespace macro::Theme
