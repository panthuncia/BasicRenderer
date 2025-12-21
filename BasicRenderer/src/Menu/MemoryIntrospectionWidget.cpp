#include "Menu/MemoryIntrospectionWidget.h"

#include <cmath>

namespace ui {

    static bool RadioButtonView(const char* label, int* v, int buttonValue) {
        ImGui::SameLine();
        return ImGui::RadioButton(label, v, buttonValue);
    }

    void MemoryIntrospectionWidget::PushFrameSample(double timeSeconds, uint64_t totalBytes) {
        rtSeries_.push(timeSeconds, (double)totalBytes);
    }

    void MemoryIntrospectionWidget::Draw(bool* pOpen,
        const MemorySnapshot* snapshot,
        const FrameGraphSnapshot* frameGraph) {
        if (pOpen && !*pOpen) return;

        if (!ImGui::Begin("Memory Introspection", pOpen)) {
            ImGui::End();
            return;
        }

        DrawToolbar();
        ImGui::Separator();

        MemorySnapshot dummyMem;
        const MemorySnapshot* ms = snapshot;
        if (ms == nullptr || (ms->categories.empty() && ms->resources.empty())) {
            dummyMem = MakeDummySnapshot();
            ms = &dummyMem;
        }
        MemorySnapshot localMem = *ms;
        if (localMem.totalBytes == 0) localMem.totalBytes = ComputeTotalBytes(localMem);

        FrameGraphSnapshot dummyFG;
        const FrameGraphSnapshot* fg = frameGraph;
        if (fg == nullptr || fg->batches.empty()) {
            dummyFG = MakeDummyFrameGraph();
            fg = &dummyFG;
        }

        switch (view_) {
        case ViewMode::Pie:      DrawPieView(localMem); break;
        case ViewMode::List:     DrawListView(localMem); break;
        case ViewMode::Timeline: DrawTimelineView(*fg);  break;
        default: break;
        }

        ImGui::End();
    }

    void MemoryIntrospectionWidget::DrawToolbar() {
        int v = (int)view_;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("View:");
        RadioButtonView("Pie", &v, (int)ViewMode::Pie);
        RadioButtonView("List", &v, (int)ViewMode::List);
        RadioButtonView("Timeline", &v, (int)ViewMode::Timeline);
        view_ = (ViewMode)v;

        ImGui::SameLine();
        ImGui::TextUnformatted("   ");
        ImGui::SameLine();
        if (ImGui::Button("Reset Settings")) {
            pie_ = {};
            list_ = {};
            rt_ = {};
            fg_ = {};
            timelineMode_ = TimelineMode::RealTime;
            selectedBatch_ = -1;
        }
    }

    // ----------------- Dummy data -----------------

    uint64_t MemoryIntrospectionWidget::ComputeTotalBytes(const MemorySnapshot& s) {
        if (!s.categories.empty()) {
            uint64_t sum = 0;
            for (auto& c : s.categories) sum += c.bytes;
            return sum;
        }
        if (!s.resources.empty()) {
            uint64_t sum = 0;
            for (auto& r : s.resources) sum += r.bytes;
            return sum;
        }
        return 0;
    }

    MemorySnapshot MemoryIntrospectionWidget::MakeDummySnapshot() {
        MemorySnapshot s;
        s.categories = {
            {"Textures/Material",        640ull * 1024 * 1024},
            {"Textures/Fullscreen RTs",  512ull * 1024 * 1024},
            {"Buffers/Vertex+Index",     160ull * 1024 * 1024},
            {"Buffers/Other",             60ull * 1024 * 1024},
            {"AccelStructs/BLAS+TLAS",   180ull * 1024 * 1024},
            {"Other/Misc",                90ull * 1024 * 1024},
        };
        s.resources = {
            {"gbuffer_albedo",     "Texture2D", 128ull * 1024 * 1024, 1001},
            {"gbuffer_normals",    "Texture2D", 128ull * 1024 * 1024, 1002},
            {"gbuffer_depth",      "Texture2D",  64ull * 1024 * 1024, 1003},
            {"material_atlas_0",   "Texture2D", 256ull * 1024 * 1024, 2001},
            {"material_atlas_1",   "Texture2D", 384ull * 1024 * 1024, 2002},
            {"meshlets",           "Buffer",     96ull * 1024 * 1024, 3002},
        };
        s.totalBytes = ComputeTotalBytes(s);
        return s;
    }

    FrameGraphSnapshot MemoryIntrospectionWidget::MakeDummyFrameGraph() {
        FrameGraphSnapshot fg;
        fg.batches = {
            {"Setup",            220ull * 1024 * 1024, false, {"Culling", "Depth Prepass"}},
            {"GBuffer",          780ull * 1024 * 1024, true,  {"VB Resolve", "GBuffer Resolve"}},
            {"Lighting",         940ull * 1024 * 1024, true,  {"Tiled Light", "SSAO", "Shadows"}},
            {"Reflections",      610ull * 1024 * 1024, false, {"SSR", "Denoise"}},
            {"Post",             520ull * 1024 * 1024, false, {"Bloom", "Tonemap", "TAA"}},
            {"UI/Compose",       180ull * 1024 * 1024, false, {"UI", "Composite"}},
        };
        return fg;
    }

    namespace {

        static inline void TrimInPlace(std::string& s) {
            auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
            while (!s.empty() && is_ws((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && is_ws((unsigned char)s.back()))  s.pop_back();
        }

        static inline void SplitMajorSub(std::string label, std::string& outMajor, std::string& outSub) {
            // Accept "Major/Sub" or "Major:Sub". If no delimiter -> Major=label, Sub=""
            TrimInPlace(label);
            size_t pos = label.find('/');
            if (pos == std::string::npos) pos = label.find(':');

            if (pos == std::string::npos) {
                outMajor = label;
                outSub.clear();
                return;
            }
            outMajor = label.substr(0, pos);
            outSub = label.substr(pos + 1);
            TrimInPlace(outMajor);
            TrimInPlace(outSub);
        }

        static inline double WrapAngle(double a) {
            const double twoPi = 6.2831853071795864769;
            while (a < 0.0) a += twoPi;
            while (a >= twoPi) a -= twoPi;
            return a;
        }

        static inline bool AngleInRange(double a, double a0, double a1) {
            // a, a0, a1 in [0,2pi). Handles wrap.
            if (a0 <= a1) return a >= a0 && a <= a1;
            // wrapped
            return a >= a0 || a <= a1;
        }

        static void DrawDonutSlice(ImDrawList* dl, ImVec2 c, float r0, float r1,
            float a0, float a1, ImU32 fill, ImU32 border, float borderThickness) {
            if (a1 <= a0 || r1 <= 0.0f) return;
            if (r0 < 0.0f) r0 = 0.0f;
            if (r0 > r1) std::swap(r0, r1);

            const float arc = a1 - a0;

            // Segment count heuristic: more segments for larger radius/arc.
            int seg = (int)std::ceil((double)arc * (double)r1 / 10.0);
            if (seg < 8) seg = 8;
            if (seg > 160) seg = 160;

            const int vtxCount = (seg + 1) * 2;
            const int idxCount = seg * 6;

            dl->PrimReserve(idxCount, vtxCount);
            const int baseVtx = dl->_VtxCurrentIdx;

            // Write vertices (outer, inner) pairs.
            for (int i = 0; i <= seg; ++i) {
                const float t = (float)i / (float)seg;
                const float a = a0 + arc * t;
                const float cs = std::cos(a);
                const float sn = std::sin(a);

                const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();

                dl->PrimWriteVtx(ImVec2(c.x + cs * r1, c.y + sn * r1), uv, fill);
                dl->PrimWriteVtx(ImVec2(c.x + cs * r0, c.y + sn * r0), uv, fill);
            }

            // Indices: two triangles per segment (quad strip), sharing vertices.
            for (int i = 0; i < seg; ++i) {
                const ImDrawIdx o0 = (ImDrawIdx)(baseVtx + i * 2 + 0);
                const ImDrawIdx i0 = (ImDrawIdx)(baseVtx + i * 2 + 1);
                const ImDrawIdx o1 = (ImDrawIdx)(baseVtx + (i + 1) * 2 + 0);
                const ImDrawIdx i1 = (ImDrawIdx)(baseVtx + (i + 1) * 2 + 1);

                dl->PrimWriteIdx(o0); dl->PrimWriteIdx(o1); dl->PrimWriteIdx(i1);
                dl->PrimWriteIdx(o0); dl->PrimWriteIdx(i1); dl->PrimWriteIdx(i0);
            }

            if (borderThickness > 0.0f) {
                // Outer arc
                dl->PathClear();
                dl->PathArcTo(c, r1, a0, a1, seg);
                dl->PathStroke(border, false, borderThickness);

                // Inner arc (only if donut, not a pie slice)
                if (r0 > 0.0f) {
                    dl->PathClear();
                    dl->PathArcTo(c, r0, a0, a1, seg);
                    dl->PathStroke(border, false, borderThickness);
                }

                // Radial edges
                const ImVec2 p0o(c.x + std::cos(a0) * r1, c.y + std::sin(a0) * r1);
                const ImVec2 p0i(c.x + std::cos(a0) * r0, c.y + std::sin(a0) * r0);
                const ImVec2 p1o(c.x + std::cos(a1) * r1, c.y + std::sin(a1) * r1);
                const ImVec2 p1i(c.x + std::cos(a1) * r0, c.y + std::sin(a1) * r0);
                dl->AddLine(p0i, p0o, border, borderThickness);
                dl->AddLine(p1i, p1o, border, borderThickness);
            }
        }

        static ImU32 Shade(ImU32 c, float factor /*0..1 darken*/) {
            const ImU32 a = (c >> IM_COL32_A_SHIFT) & 0xFF;
            ImU32 r = (c >> IM_COL32_R_SHIFT) & 0xFF;
            ImU32 g = (c >> IM_COL32_G_SHIFT) & 0xFF;
            ImU32 b = (c >> IM_COL32_B_SHIFT) & 0xFF;
            r = (ImU32)(r * factor);
            g = (ImU32)(g * factor);
            b = (ImU32)(b * factor);
            return IM_COL32(r, g, b, a);
        }

    } // anonymous

    void ui::MemoryIntrospectionWidget::DrawPieView(const MemorySnapshot& s) {
        if (ImGui::CollapsingHeader("Pie Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Hierarchical (sunburst)", &pie_.hierarchical);
            ImGui::Checkbox("Legend", &pie_.showLegend);
            ImGui::Checkbox("Percent labels (legend/tooltips)", &pie_.showPercentLabels);
            ImGui::SliderFloat("Min sub-slice % (group into Other)", &pie_.minSlicePct, 0.0f, 10.0f, "%.1f%%");
            ImGui::SliderFloat("Radius", &pie_.radius, 0.2f, 1.0f, "%.2f");

            ImGui::SeparatorText("Layout");
            ImGui::SliderFloat("Inner ratio", &pie_.innerRatio, 0.25f, 0.80f, "%.2f");
            ImGui::Checkbox("Major separators", &pie_.showMajorSeparators);
            ImGui::SliderFloat("Separator thickness", &pie_.majorSeparatorThickness, 1.0f, 6.0f, "%.1f");

            ImGui::SeparatorText("Height");
            ImGui::Checkbox("Auto height", &pie_.autoHeight);
            if (!pie_.autoHeight) {
                ImGui::SliderFloat("Pie height", &pie_.heightPx, 160.0f, 700.0f, "%.0f px");
            }
        }

        const uint64_t totalBytes = (s.totalBytes != 0) ? s.totalBytes : ComputeTotalBytes(s);
        ImGui::Text("Total: %s", FormatBytes(totalBytes).c_str());

        if (s.categories.empty()) {
            ImGui::TextUnformatted("No category data.");
            return;
        }

        // Build Major -> Sub slices from "Major/Sub" labels
        struct Sub { std::string label; uint64_t bytes; };
        struct Major { std::string label; std::vector<Sub> subs; uint64_t bytes = 0; };

        std::vector<Major> majors;
        majors.reserve(8);

        auto findMajor = [&](const std::string& m) -> Major* {
            for (auto& x : majors) if (x.label == m) return &x;
            majors.push_back(Major{ m });
            return &majors.back();
            };

        for (auto& c : s.categories) {
            if (c.bytes == 0) continue;

            std::string maj, sub;
            SplitMajorSub(c.label, maj, sub);
            if (maj.empty()) maj = "Other";

            Major* M = findMajor(maj);
            if (sub.empty()) {
                // If no subcategory provided, treat as a single sub with same label as major.
                sub = maj;
            }
            M->subs.push_back(Sub{ sub, c.bytes });
            M->bytes += c.bytes;
        }

        if (majors.empty()) {
            ImGui::TextUnformatted("No non-zero category slices.");
            return;
        }

        // sort majors by size (largest first)
        std::sort(majors.begin(), majors.end(), [](const Major& a, const Major& b) { return a.bytes > b.bytes; });

        // Adjustable plot height
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float plotH = pie_.autoHeight ? std::min(std::max(avail.y, 200.0f), 480.0f)
            : std::min(pie_.heightPx, std::max(avail.y, 120.0f));

        if (!ImPlot::BeginPlot("##MemoryPieHier", ImVec2(-1.0f, plotH),
            ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
            return;
        }

        // plot canvas setup (we'll draw in pixel space, but keep a clean plot)
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxesLimits(-1, 1, -1, 1, ImGuiCond_Always);

        ImDrawList* dl = ImPlot::GetPlotDrawList();
        const ImVec2 plotPos = ImPlot::GetPlotPos();
        const ImVec2 plotSize = ImPlot::GetPlotSize();
        const ImVec2 center = ImVec2(plotPos.x + plotSize.x * 0.5f, plotPos.y + plotSize.y * 0.5f);

        const float outerR = pie_.radius * 0.5f * std::min(plotSize.x, plotSize.y);
        const float innerR = outerR * pie_.innerRatio;

        // A small fixed palette for *majors*; subs are shaded variants
        static const ImU32 majorCols[] = {
            IM_COL32(90, 170, 255, 230),
            IM_COL32(255, 140,  90, 230),
            IM_COL32(120, 220, 140, 230),
            IM_COL32(220, 140, 255, 230),
            IM_COL32(255, 220, 120, 230),
            IM_COL32(140, 220, 220, 230),
            IM_COL32(200, 200, 200, 230),
        };
        const int paletteN = (int)(sizeof(majorCols) / sizeof(majorCols[0]));

        // Hover tracking
        int hoveredMajor = -1;
        int hoveredSub = -1;

        // Mouse polar coords
        const ImVec2 mp = ImGui::GetMousePos();
        const float dx = mp.x - center.x;
        const float dy = mp.y - center.y;
        const float mr = std::sqrt(dx * dx + dy * dy);
        double ma = WrapAngle(std::atan2((double)dy, (double)dx)); // [0,2pi)

        // Start angle at 12 o'clock (top), going clockwise
        double aCursor = -1.5707963267948966; // -pi/2
        const double twoPi = 6.2831853071795864769;

        // Stroke colors
        const ImU32 border = IM_COL32(0, 0, 0, 255);
        const ImU32 sepCol = IM_COL32(0, 0, 0, 255);

        for (int mi = 0; mi < (int)majors.size(); ++mi) {
            Major& M = majors[mi];
            const double majFrac = (totalBytes == 0) ? 0.0 : (double)M.bytes / (double)totalBytes;
            const double a0 = aCursor;
            const double a1 = aCursor + majFrac * twoPi;
            const ImU32 majorColor = majorCols[mi % paletteN];

            // --- Inner ring: major slice
            DrawDonutSlice(dl, center, 0.0f, innerR, (float)a0, (float)a1,
                majorColor, border, 1.0f);

            // Hover test for major (inner ring)
            if (mr <= innerR) {
                const double wa0 = WrapAngle(a0), wa1 = WrapAngle(a1);
                if (AngleInRange(ma, wa0, wa1)) hoveredMajor = mi;
            }

            // --- Outer ring: subs inside major
            // group tiny subs into "Other"
            std::sort(M.subs.begin(), M.subs.end(), [](const Sub& a, const Sub& b) { return a.bytes > b.bytes; });

            std::vector<Sub> subs;
            subs.reserve(M.subs.size() + 1);
            uint64_t other = 0;

            for (auto& ss : M.subs) {
                const double pctOfTotal = (totalBytes == 0) ? 0.0 : (100.0 * (double)ss.bytes / (double)totalBytes);
                if (pctOfTotal < (double)pie_.minSlicePct) other += ss.bytes;
                else subs.push_back(ss);
            }
            if (other > 0) subs.push_back(Sub{ "Other", other });

            double subCursor = a0;
            for (int si = 0; si < (int)subs.size(); ++si) {
                const Sub& S = subs[si];
                const double subFrac = (M.bytes == 0) ? 0.0 : (double)S.bytes / (double)M.bytes;
                const double sa0 = subCursor;
                const double sa1 = subCursor + subFrac * (a1 - a0);

                // shade subs progressively so they’re distinguishable inside the major
                const float shade = 0.95f - 0.08f * (float)si;
                const ImU32 subCol = Shade(majorColor, std::max(0.45f, shade));

                DrawDonutSlice(dl, center, innerR, outerR, (float)sa0, (float)sa1,
                    subCol, border, 1.0f);

                // Hover test for subs (outer ring)
                if (mr >= innerR && mr <= outerR) {
                    const double wsa0 = WrapAngle(sa0), wsa1 = WrapAngle(sa1);
                    if (AngleInRange(ma, wsa0, wsa1)) { hoveredMajor = mi; hoveredSub = si; }
                }

                subCursor = sa1;
            }

            // Major separators: radial line at start angle
            if (pie_.showMajorSeparators) {
                const float t = pie_.majorSeparatorThickness;
                const float ca = std::cos((float)a0);
                const float sa = std::sin((float)a0);
                ImVec2 p0 = center;
                ImVec2 p1 = ImVec2(center.x + outerR * ca, center.y + outerR * sa);
                dl->AddLine(p0, p1, sepCol, t);
            }

            aCursor = a1;
        }

        // draw final separator at end
        if (pie_.showMajorSeparators) {
            const float t = pie_.majorSeparatorThickness;
            const float ca = std::cos((float)aCursor);
            const float sa = std::sin((float)aCursor);
            dl->AddLine(center, ImVec2(center.x + outerR * ca, center.y + outerR * sa), sepCol, t);
        }

        // Tooltip
        if ((hoveredMajor >= 0) && ImPlot::IsPlotHovered()) {
            const auto& M = majors[hoveredMajor];

            std::string subLabel;
            uint64_t subBytes = 0;

            if (hoveredSub >= 0) {
                // rebuild the same grouped subs list to match indices (small cost, ok for now)
                std::vector<Sub> subs = M.subs;
                std::sort(subs.begin(), subs.end(), [](const Sub& a, const Sub& b) { return a.bytes > b.bytes; });

                std::vector<Sub> grouped;
                uint64_t other = 0;
                for (auto& ss : subs) {
                    const double pctOfTotal = (totalBytes == 0) ? 0.0 : (100.0 * (double)ss.bytes / (double)totalBytes);
                    if (pctOfTotal < (double)pie_.minSlicePct) other += ss.bytes;
                    else grouped.push_back(ss);
                }
                if (other > 0) grouped.push_back(Sub{ "Other", other });

                if (hoveredSub < (int)grouped.size()) {
                    subLabel = grouped[hoveredSub].label;
                    subBytes = grouped[hoveredSub].bytes;
                }
            }

            ImGui::BeginTooltip();
            ImGui::Text("%s", M.label.c_str());
            ImGui::Text("Major: %s", FormatBytes(M.bytes).c_str());
            if (!subLabel.empty()) {
                ImGui::Separator();
                ImGui::Text("Sub: %s", subLabel.c_str());
                ImGui::Text("Sub: %s", FormatBytes(subBytes).c_str());
                if (pie_.showPercentLabels && totalBytes != 0) {
                    ImGui::Text("Sub %% of total: %.2f%%", 100.0 * (double)subBytes / (double)totalBytes);
                }
            }
            else if (pie_.showPercentLabels && totalBytes != 0) {
                ImGui::Text("Major %% of total: %.2f%%", 100.0 * (double)M.bytes / (double)totalBytes);
            }
            ImGui::EndTooltip();
        }

        ImPlot::EndPlot();

        // Legend (text-based; keeps things readable even with many subs)
        if (pie_.showLegend) {
            ImGui::Separator();
            for (int mi = 0; mi < (int)majors.size(); ++mi) {
                const auto& M = majors[mi];
                const double pct = (totalBytes == 0) ? 0.0 : (100.0 * (double)M.bytes / (double)totalBytes);
                ImGui::BulletText("%s: %s%s",
                    M.label.c_str(),
                    FormatBytes(M.bytes).c_str(),
                    pie_.showPercentLabels ? (std::string("  (") + std::to_string(pct).substr(0, 4) + "%)").c_str() : "");
            }
        }
    }

    void MemoryIntrospectionWidget::DrawListView(const MemorySnapshot& s) {
        if (ImGui::CollapsingHeader("List Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            static const char* sortKeys[] = { "Size", "Name", "Type" };
            ImGui::Combo("Sort", &list_.sortKey, sortKeys, IM_ARRAYSIZE(sortKeys));
            ImGui::SameLine();
            ImGui::Checkbox("Descending", &list_.descending);
            ImGui::Checkbox("Zebra rows", &list_.zebra);
            ImGui::SliderInt("Page size", &list_.pageSize, 50, 2000);
            list_.filter.Draw("Filter (name/type)");
        }

        if (s.resources.empty()) { ImGui::TextUnformatted("No per-resource data."); return; }

        std::vector<const MemoryResourceRow*> rows;
        rows.reserve(s.resources.size());
        for (auto& r : s.resources) {
            if (list_.filter.IsActive()) {
                if (!list_.filter.PassFilter(r.name.c_str()) && !list_.filter.PassFilter(r.type.c_str()))
                    continue;
            }
            rows.push_back(&r);
        }

        auto cmpSize = [&](auto* a, auto* b) { return a->bytes < b->bytes; };
        auto cmpName = [&](auto* a, auto* b) { return a->name < b->name; };
        auto cmpType = [&](auto* a, auto* b) { return a->type < b->type; };

        switch (list_.sortKey) {
        case 0: std::sort(rows.begin(), rows.end(), cmpSize); break;
        case 1: std::sort(rows.begin(), rows.end(), cmpName); break;
        case 2: std::sort(rows.begin(), rows.end(), cmpType); break;
        default: break;
        }
        if (list_.descending) std::reverse(rows.begin(), rows.end());

        const uint64_t total = (s.totalBytes != 0) ? s.totalBytes : ComputeTotalBytes(s);
        ImGui::Text("Resources: %d   Total: %s", (int)rows.size(), FormatBytes(total).c_str());

        ImGuiTableFlags flags =
            ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

        const float tableH = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginTable("##mem_list", 5, flags, ImVec2(-1.0f, tableH))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.20f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("UID", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();

            const int pageSize = std::max(1, list_.pageSize);
            const int count = (int)rows.size();
            int shown = 0;

            ImGuiListClipper clipper;
            clipper.Begin(count);
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    if (shown++ > pageSize) break;

                    const MemoryResourceRow& r = *rows[i];
                    const double pct = (total == 0) ? 0.0 : (100.0 * (double)r.bytes / (double)total);

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r.name.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r.type.c_str());
                    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(FormatBytes(r.bytes).c_str());
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", pct);
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%llu", (unsigned long long)r.uid);
                }
            }

            if (shown > pageSize) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Showing first %d rows (increase Page size to see more).", pageSize);
            }

            ImGui::EndTable();
        }
    }

    // ----------------- Timeline view: Real-time + Frame-graph -----------------

    void MemoryIntrospectionWidget::DrawTimelineView(const FrameGraphSnapshot& fg) {
        if (ImGui::CollapsingHeader("Timeline Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            int m = (int)timelineMode_;
            ImGui::TextUnformatted("Mode:");
            ImGui::SameLine(); ImGui::RadioButton("Real-time", &m, (int)TimelineMode::RealTime);
            ImGui::SameLine(); ImGui::RadioButton("Frame-graph", &m, (int)TimelineMode::FrameGraph);
            timelineMode_ = (TimelineMode)m;

            ImGui::Separator();

            if (timelineMode_ == TimelineMode::RealTime) {
                ImGui::Checkbox("Auto-fit Y", &rt_.autoFitY);
                ImGui::Checkbox("Show markers", &rt_.showMarkers);
                ImGui::Checkbox("Sliding window", &rt_.slidingWindow);
                ImGui::SliderInt("Max seconds", &rt_.maxSeconds, 1, 60);
            }
            else {
                ImGui::SliderFloat("Bar plot height", &fg_.barPlotHeightPx, 80.0f, 260.0f, "%.0f px");
                ImGui::Checkbox("Bars use MiB", &fg_.barsUseMiB);
                ImGui::Checkbox("Show bar grid", &fg_.showBarGrid);

                ImGui::SeparatorText("Batch slot layout");
                ImGui::SliderFloat("Lane height", &fg_.laneHeight, 0.4f, 1.4f, "%.2f");
                ImGui::SliderFloat("Lane pad", &fg_.lanePad, 0.05f, 0.6f, "%.2f");

                const double kMinTransitions = 0.05, kMaxTransitions = 0.40;
                const double kMinPasses = 0.20, kMaxPasses = 1.20;
                const double kMinEnd = 0.05, kMaxEnd = 0.50;
                const double kMinGap = 0.00, kMaxGap = 0.20;
                const double kMinLeft = 0.00, kMaxLeft = 0.15;

                ImGui::SliderScalar("Transitions width", ImGuiDataType_Double,
                    &fg_.blockWidthTransitions, &kMinTransitions, &kMaxTransitions, "%.2f");
                ImGui::SliderScalar("Passes width", ImGuiDataType_Double,
                    &fg_.blockWidthPasses, &kMinPasses, &kMaxPasses, "%.2f");
                ImGui::SliderScalar("End width", ImGuiDataType_Double,
                    &fg_.blockWidthBatchEnd, &kMinEnd, &kMaxEnd, "%.2f");
                ImGui::SliderScalar("Gap", ImGuiDataType_Double,
                    &fg_.blockGap, &kMinGap, &kMaxGap, "%.2f");
                ImGui::SliderScalar("Left inset", ImGuiDataType_Double,
                    &fg_.blockLeftTransitions, &kMinLeft, &kMaxLeft, "%.2f");

                ImGui::SeparatorText("Tooltip");
                ImGui::Checkbox("Show pass list", &fg_.showPassListInTooltip);
                ImGui::SliderInt("Max tooltip passes", &fg_.maxTooltipPasses, 0, 64);
            }
        }

        if (timelineMode_ == TimelineMode::RealTime) DrawRealTimeTimeline();
        else DrawFrameGraphTimeline(fg);
    }

    void MemoryIntrospectionWidget::DrawRealTimeTimeline() {
        rtSeries_.ordered(tmpX_, tmpY_);
        if (tmpX_.size() < 2) {
            ImGui::TextUnformatted("No timeline samples yet. Call PushFrameSample(timeSeconds, totalBytes) each frame.");
            return;
        }

        if (ImPlot::BeginPlot("##MemoryTimelineRT", ImVec2(-1.0f, -1.0f), ImPlotFlags_NoTitle)) {
            ImPlot::SetupAxes("Time (s)", "Bytes", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

            if (rt_.slidingWindow) {
                const double tMax = tmpX_.back();
                const double tMin = tMax - (double)rt_.maxSeconds;
                ImPlot::SetupAxisLimits(ImAxis_X1, tMin, tMax, ImGuiCond_Always);
            }

            ImPlot::PlotLine("Total", tmpX_.data(), tmpY_.data(), (int)tmpX_.size());

            if (rt_.showMarkers) {
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                ImPlot::PlotScatter("##markers", tmpX_.data(), tmpY_.data(), (int)tmpX_.size());
            }

            ImPlot::EndPlot();
        }
    }

    // ----------------- Frame-graph timeline rendering -----------------

    std::vector<MemoryIntrospectionWidget::BatchLayout>
        MemoryIntrospectionWidget::BuildBatchLayouts(const FrameGraphSnapshot& fg,
            const FrameGraphTimelineSettings& opts,
            double* outTotalW) {
        const double wT = opts.blockWidthTransitions;
        const double wP = opts.blockWidthPasses;
        const double wE = opts.blockWidthBatchEnd;
        const double g = opts.blockGap;
        const double l = opts.blockLeftTransitions;
        const double rGutter = 0.02;

        std::vector<BatchLayout> out;
        out.resize(fg.batches.size());

        double cursor = 0.0;
        for (int i = 0; i < (int)fg.batches.size(); ++i) {
            const auto& b = fg.batches[i];
            BatchLayout bl{};
            bl.baseX = cursor;

            bl.t0 = bl.baseX + l;
            bl.t1 = bl.t0 + wT;

            bl.p0 = bl.t1 + g;
            bl.p1 = bl.p0 + wP;

            bl.hasEnd = b.hasEndTransitions;
            if (bl.hasEnd) {
                bl.e0 = bl.p1 + g;
                bl.e1 = bl.e0 + wE;
                bl.width = (bl.e1 - bl.baseX) + rGutter;
            }
            else {
                bl.width = (bl.p1 - bl.baseX) + rGutter;
            }

            out[i] = bl;
            cursor += bl.width;
        }

        if (outTotalW) *outTotalW = out.empty() ? 1.0 : (out.back().baseX + out.back().width);
        return out;
    }

    void MemoryIntrospectionWidget::DrawBlock(ImDrawList* dl,
        const ImPlotPoint& minP, const ImPlotPoint& maxP,
        ImU32 fill, ImU32 border, float rad) {
        ImVec2 a = ImPlot::PlotToPixels(minP);
        ImVec2 b = ImPlot::PlotToPixels(maxP);
        if (a.x > b.x) std::swap(a.x, b.x);
        if (a.y > b.y) std::swap(a.y, b.y);
        dl->AddRectFilled(a, b, fill, rad);
        dl->AddRect(a, b, border, rad);
    }

    bool MemoryIntrospectionWidget::IsMouseOver(const ImPlotPoint& minP, const ImPlotPoint& maxP) {
        ImVec2 mp = ImGui::GetMousePos();
        ImVec2 a = ImPlot::PlotToPixels(minP);
        ImVec2 b = ImPlot::PlotToPixels(maxP);
        if (a.x > b.x) std::swap(a.x, b.x);
        if (a.y > b.y) std::swap(a.y, b.y);
        return mp.x >= a.x && mp.x <= b.x && mp.y >= a.y && mp.y <= b.y;
    }

    void MemoryIntrospectionWidget::DrawFrameGraphTimeline(const FrameGraphSnapshot& fg) {
        if (fg.batches.empty()) {
            ImGui::TextUnformatted("No frame-graph batches.");
            return;
        }

        double totalW = 1.0;
        auto layouts = BuildBatchLayouts(fg, fg_, &totalW);

        // Split region vertically: top bars + bottom timeline
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float topH = std::min(fg_.barPlotHeightPx, std::max(80.0f, avail.y * 0.45f));
        const float botH = std::max(120.0f, avail.y - topH - 6.0f);

        // Precompute max footprint for axis limits
        uint64_t maxBytes = 0;
        for (auto& b : fg.batches) maxBytes = std::max(maxBytes, b.footprintBytes);
        const double maxY = fg_.barsUseMiB ? BytesToMiB(maxBytes) : (double)maxBytes;
        const double yPad = (maxY > 0.0) ? (maxY * 0.15) : 1.0;

        // ---- Top plot: bar graph (custom-drawn so bar width matches each slot width) ----
        if (ImPlot::BeginPlot("##BatchFootprints", ImVec2(-1.0f, topH),
            ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoLabel);
            ImPlot::SetupAxis(ImAxis_Y1, fg_.barsUseMiB ? "Footprint (MiB)" : "Footprint (bytes)",
                fg_.showBarGrid ? ImPlotAxisFlags_AutoFit : (ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoGridLines));

            ImPlot::SetupAxisLimits(ImAxis_X1, -0.1, totalW + 0.1, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, maxY + yPad, ImGuiCond_Always);

            ImDrawList* dl = ImPlot::GetPlotDrawList();

            // Vertical separators at batch boundaries (helps alignment with timeline below)
            for (int i = 0; i <= (int)layouts.size(); ++i) {
                double x = (i == (int)layouts.size()) ? totalW : layouts[i].baseX;
                ImVec2 p0 = ImPlot::PlotToPixels(ImPlotPoint(x, 0.0));
                ImVec2 p1 = ImPlot::PlotToPixels(ImPlotPoint(x, maxY + yPad));
                dl->AddLine(p0, p1, IM_COL32(180, 180, 180, 64), (i % 5 == 0) ? 2.0f : 1.0f);
            }

            // Bars (one per batch)
            for (int i = 0; i < (int)fg.batches.size(); ++i) {
                const auto& b = fg.batches[i];
                const auto& L = layouts[i];

                const double y = fg_.barsUseMiB ? BytesToMiB(b.footprintBytes) : (double)b.footprintBytes;

                ImPlotPoint minP(L.baseX, 0.0);
                ImPlotPoint maxP(L.baseX + L.width, y);

                const bool selected = (selectedBatch_ == i);
                const ImU32 fill = selected ? IM_COL32(255, 80, 80, 200) : IM_COL32(80, 160, 255, 200);
                DrawBlock(dl, minP, maxP, fill, IM_COL32(0, 0, 0, 255), 2.0f);

                // Hover / click
                if (IsMouseOver(minP, ImPlotPoint(L.baseX + L.width, maxY + yPad))) {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) selectedBatch_ = i;

                    if (ImGui::IsItemHovered() || ImPlot::IsPlotHovered()) {
                        if (ImGui::BeginTooltip()) {
                            ImGui::Text("Batch %d: %s", i, b.label.c_str());
                            ImGui::Text("Footprint: %s", FormatBytes(b.footprintBytes).c_str());
                            ImGui::EndTooltip();
                        }
                    }
                }
            }

            ImPlot::EndPlot();
        }

        ImGui::Spacing();

        // ---- Bottom plot: single-lane “frame timeline” with the same sub-blocks (T / P / optional End) ----
        if (ImPlot::BeginPlot("##FrameGraphTimeline", ImVec2(-1.0f, botH), ImPlotFlags_CanvasOnly)) {
            ImPlot::SetupAxes(nullptr, nullptr,
                ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoLabel,
                ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoLabel);

            ImPlot::SetupAxisLimits(ImAxis_X1, -0.1, totalW + 0.1, ImGuiCond_Always);

            const double y0 = 0.0;
            const double y1 = (double)fg_.lanePad + (double)fg_.laneHeight + (double)fg_.lanePad;
            ImPlot::SetupAxisLimits(ImAxis_Y1, y0, y1, ImGuiCond_Always);

            ImDrawList* dl = ImPlot::GetPlotDrawList();

            // Lane background
            DrawBlock(dl,
                ImPlotPoint(-0.25, fg_.lanePad * 0.5),
                ImPlotPoint(totalW + 0.25, fg_.lanePad + fg_.laneHeight + fg_.lanePad * 0.5),
                IM_COL32(245, 245, 245, 32),
                IM_COL32(0, 0, 0, 32),
                0.0f);

            // Batch boundary lines
            for (int i = 0; i <= (int)layouts.size(); ++i) {
                double x = (i == (int)layouts.size()) ? totalW : layouts[i].baseX;
                ImVec2 p0 = ImPlot::PlotToPixels(ImPlotPoint(x, y0));
                ImVec2 p1 = ImPlot::PlotToPixels(ImPlotPoint(x, y1));
                dl->AddLine(p0, p1, IM_COL32(180, 180, 180, 64), (i % 5 == 0) ? 2.0f : 1.0f);
            }

            const float laneY = fg_.lanePad;
            const float H = fg_.laneHeight;

            auto draw_block = [&](double x0b, double x1b, ImU32 fill, bool highlight) {
                DrawBlock(dl,
                    ImPlotPoint(x0b, laneY),
                    ImPlotPoint(x1b, laneY + H),
                    highlight ? IM_COL32(255, 80, 80, 220) : fill,
                    IM_COL32(0, 0, 0, 255),
                    4.0f);
                };

            for (int bi = 0; bi < (int)fg.batches.size(); ++bi) {
                const auto& b = fg.batches[bi];
                const auto& L = layouts[bi];
                const bool sel = (selectedBatch_ == bi);

                // transitions / passes / optional end transitions
                draw_block(L.t0, L.t1, IM_COL32(200, 100, 80, 200), sel);
                draw_block(L.p0, L.p1, IM_COL32(66, 150, 250, 200), sel);
                if (L.hasEnd) draw_block(L.e0, L.e1, IM_COL32(200, 100, 80, 200), sel);

                // Hover tooltip over the whole batch slot
                ImPlotPoint slotMin(L.baseX, laneY);
                ImPlotPoint slotMax(L.baseX + L.width, laneY + H);
                if (IsMouseOver(slotMin, slotMax)) {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) selectedBatch_ = bi;

                    ImGui::BeginTooltip();
                    ImGui::Text("Batch %d: %s", bi, b.label.c_str());
                    ImGui::Separator();
                    ImGui::Text("Footprint: %s", FormatBytes(b.footprintBytes).c_str());
                    ImGui::Text("Has End Transitions: %s", b.hasEndTransitions ? "yes" : "no");

                    if (fg_.showPassListInTooltip && !b.passNames.empty() && fg_.maxTooltipPasses != 0) {
                        ImGui::Separator();
                        ImGui::Text("Passes (%d):", (int)b.passNames.size());
                        const int maxShow = (fg_.maxTooltipPasses < 0) ? (int)b.passNames.size()
                            : std::min((int)b.passNames.size(), fg_.maxTooltipPasses);
                        for (int i = 0; i < maxShow; ++i)
                            ImGui::BulletText("%s", b.passNames[i].c_str());
                        if ((int)b.passNames.size() > maxShow)
                            ImGui::TextDisabled("...and %d more", (int)b.passNames.size() - maxShow);
                    }
                    ImGui::EndTooltip();
                }
            }

            ImPlot::EndPlot();
        }
    }

} // namespace ui
