#pragma once
// src/tables/IntelligenceTable.hpp
// ─────────────────────────────────────────────────────────────────────────────
// Reusable ImGui table widget for all seven Layer 2 intelligence panels.
// Each panel is an IntelligenceTable instance with a different domain/column set.
//
// Geo-synchronization: the table holds a copy of the current GeoSelectionContext
// and re-filters on every change. The user can override "Follow Globe Selection"
// per-table via the filter subtab.
// ─────────────────────────────────────────────────────────────────────────────
#include "../providers/IDataProvider.hpp"
#include "../app/GeoSelectionContext.hpp"
#include "../ui_common/Theme.hpp"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace macro {

// Column definition for a table panel
struct ColumnDef {
    std::string_view header;
    float            initial_width{120.0f};
    ImGuiTableColumnFlags flags{ImGuiTableColumnFlags_None};
};

// Per-table filter state
struct TableFilter {
    bool  follow_globe{true};       ///< false = manual geo override
    int   min_severity{0};          ///< 0 = all
    char  search_text[128]{};
    int   sort_col{0};
    bool  sort_asc{false};
    int   time_range_hours{24*7};   ///< 0 = all time
};

class IntelligenceTable {
public:
    using RowRenderer = std::function<void(const NormalizedRecord&, int row_idx)>;

    IntelligenceTable(std::string_view title,
                      std::string_view domain,
                      std::vector<ColumnDef> columns,
                      RowRenderer row_renderer)
        : title_(title)
        , domain_(domain)
        , columns_(std::move(columns))
        , row_renderer_(std::move(row_renderer))
    {}

    // ── Feed data in (called from UI thread via drained records) ─────────
    void ingest(const NormalizedRecord& rec) {
        if (rec.domain != domain_) return;
        std::scoped_lock lk{data_mtx_};
        // Deduplicate by record_id
        auto it = std::ranges::find_if(records_,
            [&](const NormalizedRecord& r){ return r.record_id == rec.record_id; });
        if (it != records_.end())
            *it = rec;
        else
            records_.push_back(rec);
        dirty_ = true;
    }

    // ── Update geo context (called by AppStateBus subscriber) ─────────────
    void update_context(const GeoSelectionContext& ctx) {
        if (filter_.follow_globe) {
            current_ctx_ = ctx;
            dirty_ = true;
        }
    }

    // ── Render (UI thread, called each frame) ─────────────────────────────
    void render(float width, float height) {
        // Panel container
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BG_PANEL);
        ImGui::BeginChild(title_.data(), {width, height}, true,
                          ImGuiWindowFlags_None);

        // ── Panel header ───────────────────────────────────────────────────
        ImGui::TextColored(Theme::TEXT_SECONDARY, "%s", title_.data());
        render_filter_subtab();
        ImGui::Separator();

        // ── Rebuild filtered view if dirty ─────────────────────────────────
        if (dirty_) {
            rebuild_view();
            dirty_ = false;
        }

        // ── ImGui virtualized table ────────────────────────────────────────
        ImGuiTableFlags table_flags =
            ImGuiTableFlags_ScrollY         |
            ImGuiTableFlags_RowBg           |
            ImGuiTableFlags_BordersInnerH   |
            ImGuiTableFlags_Sortable        |
            ImGuiTableFlags_SortMulti       |
            ImGuiTableFlags_Hideable        |
            ImGuiTableFlags_Resizable       |
            ImGuiTableFlags_SizingStretchProp;

        float table_h = height - 90.0f; // subtract header/filter
        if (ImGui::BeginTable(title_.data(),
                              static_cast<int>(columns_.size()),
                              table_flags, {width - 16.0f, table_h})) {
            // Setup columns
            for (auto& col : columns_) {
                ImGui::TableSetupColumn(col.header.data(),
                                        col.flags | ImGuiTableColumnFlags_WidthFixed,
                                        col.initial_width);
            }
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Handle sort
            if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs(); ss && ss->SpecsDirty) {
                ss->SpecsDirty = false;
                // Re-sort filtered_view_ by column index
                if (ss->SpecsCount > 0) {
                    int sort_col  = ss->Specs[0].ColumnIndex;
                    bool sort_asc = ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                    sort_view(sort_col, sort_asc);
                }
            }

            // Virtualized rows via ImGuiListClipper
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(filtered_view_.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    ImGui::TableNextRow();
                    if (row < static_cast<int>(filtered_view_.size()))
                        row_renderer_(*filtered_view_[row], row);
                }
            }
            clipper.End();
            ImGui::EndTable();
        }

        // ── Provenance footer ──────────────────────────────────────────────
        ImGui::Separator();
        auto since = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - last_refresh_).count();
        bool stale = since > 300;
        ImGui::TextColored(stale ? Theme::TEXT_STALE : Theme::TEXT_MUTED,
                           "%zu records · refreshed %llds ago",
                           filtered_view_.size(), since);

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    [[nodiscard]] int record_count() const {
        std::scoped_lock lk{data_mtx_};
        return static_cast<int>(records_.size());
    }

private:
    std::string_view       title_;
    std::string_view       domain_;
    std::vector<ColumnDef> columns_;
    RowRenderer            row_renderer_;
    TableFilter            filter_;
    GeoSelectionContext    current_ctx_;

    mutable std::mutex                 data_mtx_;
    std::vector<NormalizedRecord>      records_;
    std::vector<const NormalizedRecord*> filtered_view_;
    bool                               dirty_{false};
    std::chrono::system_clock::time_point last_refresh_{std::chrono::system_clock::now()};

    void render_filter_subtab() {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200.0f);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputText("##search", filter_.search_text, sizeof(filter_.search_text));
        ImGui::SameLine();
        ImGui::Checkbox("Follow Globe", &filter_.follow_globe);
    }

    void rebuild_view() {
        std::scoped_lock lk{data_mtx_};
        filtered_view_.clear();
        filtered_view_.reserve(records_.size());

        std::string_view search{filter_.search_text};

        for (const auto& rec : records_) {
            // Severity filter
            if (rec.severity < filter_.min_severity) continue;

            // Geo filter
            if (filter_.follow_globe &&
                current_ctx_.resolution >= GeoResolution::Country &&
                !current_ctx_.country_iso2.empty() &&
                rec.geo.country_iso2 &&
                *rec.geo.country_iso2 != current_ctx_.country_iso2)
                continue;

            // Text search
            if (!search.empty()) {
                bool found = rec.headline.find(search) != std::string::npos ||
                             rec.source_name.find(search) != std::string::npos;
                if (!found) continue;
            }

            filtered_view_.push_back(&rec);
        }

        last_refresh_ = std::chrono::system_clock::now();
    }

    void sort_view(int col_idx, bool ascending) {
        // Simple sort by severity (col 0) or timestamp (last col)
        // Full domain-specific sort can be extended per table
        std::ranges::sort(filtered_view_,
            [col_idx, ascending](const NormalizedRecord* a, const NormalizedRecord* b) {
                int cmp = 0;
                if (col_idx == 0)
                    cmp = a->severity <=> b->severity == std::strong_ordering::less ? -1 : 1;
                else
                    cmp = a->timestamp < b->timestamp ? -1 : 1;
                return ascending ? cmp < 0 : cmp > 0;
            });
    }
};

} // namespace macro
