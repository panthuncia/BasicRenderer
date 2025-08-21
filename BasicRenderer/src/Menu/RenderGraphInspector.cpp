#include "Menu/RenderGraphInspector.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <tuple>
#include <sstream>

#include "Render/QueueKind.h"
#include "Resources/ResourceStates.h"

enum class SignalPhase : uint8_t { AfterTransitions, AfterPasses };

struct SignalSite {
    int        batchIndex = -1;
    QueueKind  queue = QueueKind::Graphics;
    SignalPhase phase = SignalPhase::AfterTransitions;
};

// (QueueKind,fenceValue) -> SignalSite
using SignalIndexKey = std::pair<QueueKind, uint64_t>;
struct SignalIndexKeyHash {
    size_t operator()(const SignalIndexKey& k) const noexcept {
        return (size_t)k.first ^ (size_t)(k.second * 1469598103934665603ull);
    }
};
using SignalIndex = std::unordered_map<SignalIndexKey, SignalSite, SignalIndexKeyHash>;

static SignalIndex BuildSignalIndex(const std::vector<RenderGraph::PassBatch>& batches) {
    SignalIndex idx;
    for (int i = 0; i < (int)batches.size(); ++i) {
        const auto& b = batches[i];

        if (b.renderTransitionSignal) {
            idx[{QueueKind::Graphics, b.renderTransitionFenceValue}] =
            { i, QueueKind::Graphics, SignalPhase::AfterTransitions };
        }
        if (b.renderCompletionSignal) {
            idx[{QueueKind::Graphics, b.renderCompletionFenceValue}] =
            { i, QueueKind::Graphics, SignalPhase::AfterPasses };
        }
        if (b.computeTransitionSignal) {
            idx[{QueueKind::Compute, b.computeTransitionFenceValue}] =
            { i, QueueKind::Compute, SignalPhase::AfterTransitions };
        }
        if (b.computeCompletionSignal) {
            idx[{QueueKind::Compute, b.computeCompletionFenceValue}] =
            { i, QueueKind::Compute, SignalPhase::AfterPasses };
        }
        // (Add copy lane when you have it.)
    }
    return idx;
}

// ---- Helpers to fetch all resource IDs present (for the side list) ----
static void CollectResourceIds(const std::vector<RenderGraph::PassBatch>& batches,
    std::unordered_map<uint64_t, std::string>& outIdToName)
{
    for (const auto& b : batches) {
        auto scan = [&](const std::vector<ResourceTransition>& v) {
            for (auto& t : v) {
                if (!t.pResource) continue;
                outIdToName.emplace(t.pResource->GetGlobalResourceID(), ws2s(t.pResource->GetName()));
            }
            };
        scan(b.renderTransitions);
        scan(b.computeTransitions);

        // Also add anything the batch knows about:
        for (auto id : b.allResources) outIdToName.emplace(id, std::string{});
        for (auto id : b.internallyTransitionedResources) outIdToName.emplace(id, std::string{});
    }
}

// Colors
static ImU32 ColPassGraphics() { return IM_COL32(66, 150, 250, 200); }
static ImU32 ColPassCompute() { return IM_COL32(100, 200, 100, 200); }
static ImU32 ColPassCopy() { return IM_COL32(200, 200, 100, 200); }
static ImU32 ColTrans() { return IM_COL32(200, 100, 80, 200); }
static ImU32 ColArrowWait() { return IM_COL32(255, 200, 64, 220); }
static ImU32 ColHighlight() { return IM_COL32(255, 64, 64, 255); }
static ImU32 ColBorder() { return IM_COL32(0, 0, 0, 255); }

static float LaneY(QueueKind qk, float rowHeight, float laneSpacing) {
    // y from top: Graphics (2), Compute (1), Copy(0) to show graphics on top
    int laneIndex = (qk == QueueKind::Graphics) ? 2 : (qk == QueueKind::Compute ? 1 : 0);
    return laneIndex * (rowHeight + laneSpacing);
}

static void DrawBlock(ImDrawList* dl, const ImPlotPoint& minP, const ImPlotPoint& maxP,
    ImU32 fill, ImU32 border, float rad = 4.0f)
{
    // Convert to screen px
    ImVec2 a = ImPlot::PlotToPixels(minP);
    ImVec2 b = ImPlot::PlotToPixels(maxP);
    // Clamp (just in case)
    if (a.x > b.x) std::swap(a.x, b.x);
    if (a.y > b.y) std::swap(a.y, b.y);
    dl->AddRectFilled(a, b, fill, rad);
    dl->AddRect(a, b, border, rad);
}

static bool IsMouseOver(const ImPlotPoint& minP, const ImPlotPoint& maxP) {
    ImVec2 mp = ImGui::GetMousePos();
    ImVec2 a = ImPlot::PlotToPixels(minP);
    ImVec2 b = ImPlot::PlotToPixels(maxP);
    if (a.x > b.x) std::swap(a.x, b.x);
    if (a.y > b.y) std::swap(a.y, b.y);
    return mp.x >= a.x && mp.x <= b.x && mp.y >= a.y && mp.y <= b.y;
}

static void DrawArrowBetweenLanes(ImDrawList* dl,
    float x0, float y0,
    float x1, float y1,
    ImU32 color,
    const char* label)
{
    // Convert to screen space once
    ImVec2 p0 = ImPlot::PlotToPixels(ImPlotPoint(x0, y0));
    ImVec2 p1 = ImPlot::PlotToPixels(ImPlotPoint(x1, y1));

    // Build gentle horizontal-ish bezier (handles either direction)
    float dx = (p1.x - p0.x);
    float dy = (p1.y - p0.y);

    // Curve magnitude: bias by |dy| so farther lanes arc more
    float cx = 0.30f * std::fabs(dy) + 12.0f;

    // Control points nudge horizontally toward the middle
    ImVec2 c0 = ImVec2(p0.x + (dx >= 0 ? cx : -cx), p0.y);
    ImVec2 c1 = ImVec2(p1.x - (dx >= 0 ? cx : -cx), p1.y);

    dl->AddBezierCubic(p0, c0, c1, p1, color, 2.0f);

    // Arrow head at the end, aligned to outgoing tangent
    ImVec2 tan = ImVec2(p1.x - c1.x, p1.y - c1.y);
    float len = std::sqrt(tan.x * tan.x + tan.y * tan.y);
    if (len > 1e-3f) { tan.x /= len; tan.y /= len; }
    else { tan = ImVec2(1, 0); }
    ImVec2 nrm = ImVec2(-tan.y, tan.x);

    const float headLen = 10.0f;
    const float headWide = 5.0f;
    ImVec2 a = p1;
    ImVec2 b = ImVec2(p1.x - tan.x * headLen + nrm.x * headWide,
        p1.y - tan.y * headLen + nrm.y * headWide);
    ImVec2 c = ImVec2(p1.x - tan.x * headLen - nrm.x * headWide,
        p1.y - tan.y * headLen - nrm.y * headWide);
    dl->AddTriangleFilled(a, b, c, color);

    // Optional label near the mid of the curve
    if (label && *label) {
        ImVec2 mid = ImVec2((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f - 12.0f);
        dl->AddText(mid, color, label);
    }
}

namespace RGInspector {

    void Show(const std::vector<RenderGraph::PassBatch>& batches,
        RGPassUsesResourceFn passUses,
        const RGInspectorOptions& opts)
    {
        if (!ImGui::Begin("Render Graph Inspector")) { ImGui::End(); return; }

        SignalIndex sigIdx = BuildSignalIndex(batches);

        // --- Left panel: resource picker ---
        static uint64_t s_selectedRes = 0;
        static char filterBuf[128] = {};
        std::unordered_map<uint64_t, std::string> idToName;
        CollectResourceIds(batches, idToName);

        ImGui::BeginChild("LeftPanel", ImVec2(320, 0), true);
        ImGui::TextUnformatted("Resources");
        ImGui::InputTextWithHint("##resfilter", "filter...", filterBuf, IM_ARRAYSIZE(filterBuf));

        std::string f = filterBuf;
        std::transform(f.begin(), f.end(), f.begin(), ::tolower);

        for (auto& kv : idToName) {
            const uint64_t id = kv.first;
            std::string label = kv.second.empty() ? ("#" + std::to_string(id)) : kv.second + " [" + std::to_string(id) + "]";
            std::string lcl = label; std::transform(lcl.begin(), lcl.end(), lcl.begin(), ::tolower);
            if (!f.empty() && lcl.find(f) == std::string::npos) continue;

            bool sel = (s_selectedRes == id);
            if (ImGui::Selectable(label.c_str(), sel)) s_selectedRes = id;
            if (ImGui::IsItemHovered() && !kv.second.empty())
                ImGui::SetTooltip("%s", kv.second.c_str());
        }
        if (ImGui::Button("Clear Selection")) s_selectedRes = 0;
        ImGui::EndChild();

        ImGui::SameLine();

        // --- Right panel: plot ---
        ImGui::BeginGroup();
        if (ImPlot::BeginPlot("##RGPlot", ImVec2(-1, -1), ImPlotFlags_CanvasOnly)) {
            // Axes: X = batch index [0..N], Y = lanes
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
            ImPlot::SetupAxisLimits(ImAxis_X1, -0.25, (double)batches.size() + 0.25, ImGuiCond_Always);
            // Three lanes stacked (Copy at bottom, Compute middle, Graphics top)
            const float H = opts.rowHeight;
            const float S = opts.laneSpacing;
            ImPlot::SetupAxisLimits(ImAxis_Y1, -S, LaneY(QueueKind::Graphics, H, S) + H + S, ImGuiCond_Always);

            ImDrawList* dl = ImPlot::GetPlotDrawList();

            // Grid: lane separators + labels
            auto draw_lane_bg = [&](QueueKind qk, const char* name) {
                float y = LaneY(qk, H, S);
                ImPlotPoint a(-0.25, y - 0.05);
                ImPlotPoint b((double)batches.size() + 0.25, y + H + 0.05);
                DrawBlock(dl, a, b, IM_COL32(245, 245, 245, 32), IM_COL32(0, 0, 0, 32), 0.0f);
                // label on the left
                ImVec2 lp = ImPlot::PlotToPixels(ImPlotPoint(-0.2, y + 0.1));
                dl->AddText(lp, IM_COL32_BLACK, name);
                };
            draw_lane_bg(QueueKind::Copy, "Copy");
            draw_lane_bg(QueueKind::Compute, "Compute");
            draw_lane_bg(QueueKind::Graphics, "Graphics");

            // X grid lines per batch
            for (int i = 0; i <= (int)batches.size(); ++i) {
                ImVec2 p0 = ImPlot::PlotToPixels(ImPlotPoint(i, -S));
                ImVec2 p1 = ImPlot::PlotToPixels(ImPlotPoint(i, LaneY(QueueKind::Graphics, H, S) + H + S));
                dl->AddLine(p0, p1, IM_COL32(180, 180, 180, 64), (i % 5 == 0) ? 2.0f : 1.0f);
            }

            // Draw passes + transitions per batch
            const float xT0 = opts.blockLeftTransitions;
            const float xT1 = xT0 + opts.blockWidthTransitions;
            const float xP0 = xT1 + opts.blockGap;
            const float xP1 = xP0 + opts.blockWidthPasses;

            const float yG = LaneY(QueueKind::Graphics, H, S) + H * 0.5f;
            const float yC = LaneY(QueueKind::Compute, H, S) + H * 0.5f;

            auto draw_transitions = [&](const std::vector<ResourceTransition>& v, QueueKind qk, int batchIndex) {
                if (v.empty()) return;

                const bool haveSelection = (s_selectedRes != 0);
                float y = LaneY(qk, H, S);
                ImPlotPoint minP(batchIndex + xT0, y);
                ImPlotPoint maxP(batchIndex + xT1, y + H);

                // Find which transitions in this block touch the selected resource
                std::vector<int> matchIdx;
                matchIdx.reserve(v.size());
                for (int i = 0; i < (int)v.size(); ++i) {
                    const auto& t = v[i];
                    if (!t.pResource) continue;
                    if (haveSelection &&t.pResource->GetGlobalResourceID() == s_selectedRes)
                        matchIdx.push_back(i);
                }

                const bool touchesSelected = !matchIdx.empty();
                DrawBlock(dl, minP, maxP, touchesSelected ? ColHighlight() : ColTrans(), ColBorder());

                // Draw thin inner bars for each *matching* transition so you can see count/position
                if (!matchIdx.empty()) {
                    const int n = (int)v.size();
                    for (int k = 0; k < (int)matchIdx.size(); ++k) {
                        int i = matchIdx[k];
                        double xi = (batchIndex + xT0) + (opts.blockWidthTransitions) * ((i + 0.5) / (double)n);
                        ImPlotPoint p0(xi - 0.012, y + 0.15 * H);
                        ImPlotPoint p1(xi + 0.012, y + 0.85 * H);
                        DrawBlock(dl, p0, p1, IM_COL32(20, 20, 20, 220), IM_COL32(255, 255, 255, 200), 3.0f);
                    }
                }

                // Tooltip: when hovering the block, list details for matching transitions (or all if nothing selected)
                if (IsMouseOver(minP, maxP)) {
                    const bool showAll = !haveSelection;  // fall back to all transitions if nothing selected
                    if (showAll || !matchIdx.empty()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted("Transitions");
                        ImGui::Separator();

                        auto emit = [&](int i) {
                            const auto& t = v[i];
                            std::string name = ws2s(t.pResource->GetName());
                            ImGui::Text("%s (%llu)", name.c_str(),
                                (unsigned long long)t.pResource->GetGlobalResourceID());
                            // Adjust these fields to your actual range struct
                            ImGui::BulletText("Subresource: mip[%u..%u], array[%u..%u]",
                                t.range.mipLower, t.range.mipUpper,
                                t.range.sliceLower, t.range.sliceUpper);
                            ImGui::BulletText("Layout : %s -> %s", BarrierLayoutToString(ResourceLayoutToD3D12(t.prevLayout)), BarrierLayoutToString(ResourceLayoutToD3D12(t.newLayout)));
                            ImGui::BulletText("Access : %s -> %s", BarrierAccessToString(ResourceAccessTypeToD3D12(t.prevAccessType)), BarrierAccessToString(ResourceAccessTypeToD3D12(t.newAccessType)));
                            ImGui::BulletText("Sync   : %s -> %s", BarrierSyncToString(ResourceSyncStateToD3D12(t.prevSyncState)), BarrierSyncToString(ResourceSyncStateToD3D12(t.newSyncState)));
                            ImGui::Separator();
                            };

                        if (haveSelection) {
                            for (int i : matchIdx) emit(i);
                        }
                        else {
                            // nothing selected: show a compact view of all transitions in this block
                            const int maxShow = 12; // avoid huge tooltips
                            int shown = 0;
                            for (int i = 0; i < (int)v.size() && shown < maxShow; ++i, ++shown) emit(i);
                            if ((int)v.size() > maxShow)
                                ImGui::TextDisabled("...and %d more", (int)v.size() - maxShow);
                        }
                        ImGui::EndTooltip();
                    }
                }
                };

            auto draw_passes = [&](auto const& passesVec, QueueKind qk, int batchIndex, bool isCompute) {
                if (passesVec.empty()) return;
                float y = LaneY(qk, H, S);
                ImPlotPoint minP(batchIndex + xP0, y);
                ImPlotPoint maxP(batchIndex + xP1, y + H);

                // Does any pass use the selected resource?
                bool touchesSelected = false;
                if (s_selectedRes != 0 && passUses) {
                    for (auto const& pr : passesVec)
                        if (passUses((const void*)&pr, s_selectedRes, isCompute)) { touchesSelected = true; break; }
                }

                DrawBlock(dl, minP, maxP,
                    touchesSelected ? ColHighlight() :
                    (qk == QueueKind::Graphics ? ColPassGraphics() :
                        qk == QueueKind::Compute ? ColPassCompute() : ColPassCopy()),
                    ColBorder());

                // Pass labels (stacked vertically)
                int n = (int)passesVec.size();
                ImVec2 tp = ImPlot::PlotToPixels(ImPlotPoint(batchIndex + (xP0 + xP1) * 0.5, y + 0.1 + 0.5 * (H - 0.2)));
                dl->AddText(tp, IM_COL32_BLACK, std::to_string(n).c_str());
                //for (int i = 0; i < n; ++i) {
                //    double u = (i + 0.5) / std::max(1, n);
                //    ImVec2 tp = ImPlot::PlotToPixels(ImPlotPoint(batchIndex + (xP0 + xP1) * 0.5, y + 0.1 + u * (H - 0.2)));
                //    const char* name = passesVec[i].name.c_str();
                //    dl->AddText(tp, IM_COL32_BLACK, name);
                //}

                // Tooltip for whole pass block
                if (IsMouseOver(minP, maxP)) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s Passes (%d)", qk == QueueKind::Graphics ? "Graphics" : (qk == QueueKind::Compute ? "Compute" : "Copy"), (int)passesVec.size());
                    for (auto const& pr : passesVec) ImGui::BulletText("%s", pr.name.c_str());
                    ImGui::EndTooltip();
                }
                };

            // Draw all batches
            for (int bi = 0; bi < (int)batches.size(); ++bi) {
                const auto& b = batches[bi];

                // Compute lane
                draw_transitions(b.computeTransitions, QueueKind::Compute, bi);
                draw_passes(b.computePasses, QueueKind::Compute, bi, /*isCompute*/true);

                // Graphics lane
                draw_transitions(b.renderTransitions, QueueKind::Graphics, bi);
                draw_passes(b.renderPasses, QueueKind::Graphics, bi, /*isCompute*/false);

                // Optional Copy lane (you can add b.copyTransitions/passes later)

                // Cross-queue waits/signals (we draw at the left edge of the “execution” in each lane)
                auto laneYG = LaneY(QueueKind::Graphics, H, S) + H * 0.5f;
                auto laneYC = LaneY(QueueKind::Compute, H, S) + H * 0.5f;

                auto labelFence = [](bool enabled, uint64_t val)->std::string {
                    if (!enabled) return {};
                    std::ostringstream o; o << "#" << val; return o.str();
                    };

                auto siteToX = [&](const SignalSite& s)->float {
                    if (s.phase == SignalPhase::AfterTransitions) return s.batchIndex + xT1;
                    else                                          return s.batchIndex + xP1;
                    };
                auto laneCenterY = [&](QueueKind q)->float {
                    return (q == QueueKind::Graphics) ? yG : (q == QueueKind::Compute ? yC : (yC - (yG - yC))); // adjust if/when you add Copy
                    };

                if (b.computeQueueWaitOnRenderQueueBeforeTransition) {
                    uint64_t fv = b.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue;
                    auto it = sigIdx.find({ QueueKind::Graphics, fv });
                    if (it != sigIdx.end()) {
                        const SignalSite& src = it->second;
                        float x0 = siteToX(src);             // where GRAPHICS actually signaled 'fv'
                        float y0 = laneCenterY(src.queue);
                        float x1 = bi + xT0;                 // COMPUTE waits at the start of its transitions
                        float y1 = yC;
                        auto lbl = labelFence(true, fv);
                        DrawArrowBetweenLanes(dl, x0, y0, x1, y1, ColArrowWait(), lbl.c_str());
                    }
                    else {
                        // Optional: draw a small warning glyph at (bi + xT0, yC)
                    }
                }

                // --- Compute waits on Graphics, BEFORE EXECUTION ---
                if (b.computeQueueWaitOnRenderQueueBeforeExecution) {
                    uint64_t fv = b.computeQueueWaitOnRenderQueueBeforeExecutionFenceValue;
                    auto it = sigIdx.find({ QueueKind::Graphics, fv });
                    if (it != sigIdx.end()) {
                        const SignalSite& src = it->second;
                        float x0 = siteToX(src);
                        float y0 = laneCenterY(src.queue);
                        float x1 = bi + xP0;                 // COMPUTE waits at the start of its passes
                        float y1 = yC;
                        auto lbl = labelFence(true, fv);
                        DrawArrowBetweenLanes(dl, x0, y0, x1, y1, ColArrowWait(), lbl.c_str());
                    }
                }

                // --- Graphics waits on Compute, BEFORE TRANSITIONS ---
                if (b.renderQueueWaitOnComputeQueueBeforeTransition) {
                    uint64_t fv = b.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue;
                    auto it = sigIdx.find({ QueueKind::Compute, fv });
                    if (it != sigIdx.end()) {
                        const SignalSite& src = it->second;
                        float x0 = siteToX(src);
                        float y0 = laneCenterY(src.queue);
                        float x1 = bi + xT0;                 // GRAPHICS waits at the start of its transitions
                        float y1 = yG;
                        auto lbl = labelFence(true, fv);
                        DrawArrowBetweenLanes(dl, x0, y0, x1, y1, ColArrowWait(), lbl.c_str());
                    }
                }

                // --- Graphics waits on Compute, BEFORE EXECUTION ---
                if (b.renderQueueWaitOnComputeQueueBeforeExecution) {
                    uint64_t fv = b.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue;
                    auto it = sigIdx.find({ QueueKind::Compute, fv });
                    if (it != sigIdx.end()) {
                        const SignalSite& src = it->second;
                        float x0 = siteToX(src);
                        float y0 = laneCenterY(src.queue);
                        float x1 = bi + xP0;                 // GRAPHICS waits at the start of its passes
                        float y1 = yG;
                        auto lbl = labelFence(true, fv);
                        DrawArrowBetweenLanes(dl, x0, y0, x1, y1, ColArrowWait(), lbl.c_str());
                    }
                }
            }

            ImPlot::EndPlot();
        }
        ImGui::EndGroup();

        ImGui::End();
    }

} // namespace RGInspector
