#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Resources/ReadbackRequest.h"
#include "Resources/ResourceStateTracker.h"

class Resource;
struct LayoutNode;

namespace ui {

    using MemoryViewRequestCaptureFn = std::function<void(const std::string&, Resource*, const RangeSpec&, ReadbackCaptureCallback)>;

    class MemoryViewWidget {
    public:
        // Schedules a readback capture and opens the window.
        void Open(const std::string& passName, Resource* resource, const RangeSpec& range, MemoryViewRequestCaptureFn requestCapture);

        // Draws the window if open.
        void Draw(bool* pOpen);

    private:
        void DrawBufferView(const ReadbackCaptureResult& r);
        void DrawTextureViewStub(const ReadbackCaptureResult& r);
        void SaveCurrentResourceLayoutState();
        void LoadResourceLayoutState(uint64_t resourceId);

        struct PendingRequest {
            std::string passName;
            Resource* resource = nullptr;
            RangeSpec range{};
            uint64_t resourceId = 0;
            std::string resourceName;
        };

        std::mutex mutex_;
        std::optional<PendingRequest> pending_;
        std::optional<ReadbackCaptureResult> result_;
        bool waiting_ = false;
        std::string status_;

        // UI state
        int bytesPerRow_ = 16;
        std::array<char, 16 * 1024> structInputBuf_{};
        std::string reflectionDiagnostics_;
        size_t reflectedRootSizeBytes_ = 0;
        size_t reflectedRootStrideBytes_ = 0;
        bool reflectionValid_ = false;
        std::shared_ptr<LayoutNode> reflectedRoot_;

        int goToElementInput_ = 0;
        int scrollToElement_ = -1;

        uint64_t goToByteOffsetInput_ = 0;
        uint64_t scrollToByteOffset_ = UINT64_MAX;
        uint64_t highlightedByteOffset_ = UINT64_MAX;

        uint64_t currentResourceId_ = 0;

        struct ResourceLayoutState {
            std::string structInput;
            std::string diagnostics;
            size_t rootSizeBytes = 0;
            size_t rootStrideBytes = 0;
            bool reflectionValid = false;
            std::shared_ptr<LayoutNode> reflectedRoot;
            int goToElementInput = 0;
        };

        std::unordered_map<uint64_t, ResourceLayoutState> perResourceLayoutState_;
    };

} // namespace ui
