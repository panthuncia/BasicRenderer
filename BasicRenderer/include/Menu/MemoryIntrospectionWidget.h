#pragma once
#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

    static inline std::string FormatBytes(uint64_t bytes) {
        constexpr double KB = 1024.0;
        constexpr double MB = 1024.0 * 1024.0;
        constexpr double GB = 1024.0 * 1024.0 * 1024.0;

        char buf[64]{};
        if (bytes >= (uint64_t)GB) {
            std::snprintf(buf, sizeof(buf), "%.2f GiB", (double)bytes / GB);
        }
        else if (bytes >= (uint64_t)MB) {
            std::snprintf(buf, sizeof(buf), "%.2f MiB", (double)bytes / MB);
        }
        else if (bytes >= (uint64_t)KB) {
            std::snprintf(buf, sizeof(buf), "%.2f KiB", (double)bytes / KB);
        }
        else {
            std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
        }
        return std::string(buf);
    }

    static inline double BytesToMiB(uint64_t bytes) {
        return (double)bytes / (1024.0 * 1024.0);
    }

    struct MemoryCategorySlice {
        std::string label;
        uint64_t bytes = 0;
    };

    struct MemoryResourceRow {
        std::string name;
        std::string type;
        uint64_t bytes = 0;
        uint64_t uid = 0;
    };

    struct MemorySnapshot {
        std::vector<MemoryCategorySlice> categories;
        std::vector<MemoryResourceRow>   resources;
        uint64_t totalBytes = 0;
    };

    // Frame-graph timeline input
    struct FrameGraphBatchRow {
        std::string label;
        uint64_t footprintBytes = 0;  // memory footprint for this batch (lower-bound)
        bool hasEndTransitions = false;
    	std::vector<std::string> passNames;
        std::vector<MemoryCategorySlice> categories;
    };

    struct FrameGraphSnapshot {
        std::vector<FrameGraphBatchRow> batches;
    };

    // Simple ring buffer for real-time timeline
    template <size_t N>
    struct RingSeries {
        std::array<double, N> x{};
        std::array<double, N> y{};
        size_t head = 0;
        size_t count = 0;

        void push(double xv, double yv) {
            x[head] = xv;
            y[head] = yv;
            head = (head + 1) % N;
            count = (count < N) ? (count + 1) : N;
        }

        void ordered(std::vector<double>& outX, std::vector<double>& outY) const {
            outX.clear(); outY.clear();
            outX.reserve(count); outY.reserve(count);
            if (count == 0) return;

            const size_t start = (count == N) ? head : 0;
            for (size_t i = 0; i < count; ++i) {
                const size_t idx = (start + i) % N;
                outX.push_back(x[idx]);
                outY.push_back(y[idx]);
            }
        }
    };

    class MemoryIntrospectionWidget {
    public:
        // Feed the real-time total (seconds, bytes).
        void PushFrameSample(double timeSeconds, uint64_t totalBytes);

        // Draw the window. `frameGraph` is optional; if nullptr/empty we show a dummy.
        void Draw(bool* pOpen,
            const MemorySnapshot* snapshot = nullptr,
            const FrameGraphSnapshot* frameGraph = nullptr);

    private:
        enum class ViewMode : int { Pie = 0, List = 1, Timeline = 2 };
        enum class TimelineMode : int { RealTime = 0, FrameGraph = 1 };

        struct PieSettings {
            float minSlicePct = 1.0f;      // applies to *sub-slices* within each major
            float radius = 0.92f;

            bool autoHeight = true;
            float heightPx = 300.0f;       // used when autoHeight == false

            float innerRatio = 0.55f;      // inner ring radius = outerR * innerRatio
            bool showMajorSeparators = true;
            float majorSeparatorThickness = 3.0f;
        };

        struct ListSettings {
            bool descending = true;
            int sortKey = 0; // 0=size, 1=name, 2=type
            ImGuiTextFilter filter;
            int pageSize = 2000; // TODO: Dynamic window?
        };

        struct RealTimeTimelineSettings {
            int maxSeconds = 10;
        };

        struct FrameGraphTimelineSettings {
            // Plot split
            float barPlotHeightPx = 140.0f;

            // Batch slot layout in "plot X units"
            double blockWidthTransitions = 0.18;
            double blockWidthPasses = 0.62;
            double blockWidthBatchEnd = 0.22;   // used only if hasEndTransitions
            double blockGap = 0.04;
            double blockLeftTransitions = 0.02;

            // Y layout for bottom timeline lane
            float laneHeight = 0.85f;    // in plot Y units
            float lanePad = 0.15f;       // in plot Y units

            // Bars
            bool showBarGrid = true;

            // Interaction / display
            bool showBatchNamesOnHover = true;
            bool showPassListInTooltip = true;
            int  maxTooltipPasses = 12;
        };

    private:
        // Layout used for the frame-graph timeline (single lane, but same slot subdivision).
        struct BatchLayout {
            double baseX = 0.0;
            double width = 0.0;

            double t0 = 0.0, t1 = 0.0; // transitions
            double p0 = 0.0, p1 = 0.0; // passes
            double e0 = 0.0, e1 = 0.0; // end transitions
            bool   hasEnd = false;
        };

    private:
        static MemorySnapshot MakeDummySnapshot();
        static FrameGraphSnapshot MakeDummyFrameGraph();
        static uint64_t ComputeTotalBytes(const MemorySnapshot& s);

        void DrawToolbar();
        void DrawPieView(const MemorySnapshot& s);
        void DrawListView(const MemorySnapshot& s);

        void DrawTimelineView(const FrameGraphSnapshot& fg, const MemorySnapshot& ms);
        void DrawRealTimeTimeline();
        void DrawFrameGraphTimeline(const FrameGraphSnapshot& fg, const MemorySnapshot& ms);

        static std::vector<BatchLayout> BuildBatchLayouts(const FrameGraphSnapshot& fg,
            const FrameGraphTimelineSettings& opts,
            double* outTotalW);

        // Plot-rect helpers
        static void DrawBlock(ImDrawList* dl,
            const ImPlotPoint& minP, const ImPlotPoint& maxP,
            ImU32 fill, ImU32 border, float rad = 4.0f);

        static bool IsMouseOver(const ImPlotPoint& minP, const ImPlotPoint& maxP);

    private:
        ViewMode view_ = ViewMode::Pie;
        TimelineMode timelineMode_ = TimelineMode::RealTime;

        PieSettings pie_;
        ListSettings list_;

        RealTimeTimelineSettings rt_;
        FrameGraphTimelineSettings fg_;

        RingSeries<600> rtSeries_;
        std::vector<double> tmpX_;
        std::vector<double> tmpY_;
        std::vector<double> tmpYScaled_;
        double rtLastCommittedTime_ = -1.0;

        int selectedBatch_ = -1;
    };

} // namespace ui
