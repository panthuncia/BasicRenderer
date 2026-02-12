#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "Resources/ReadbackRequest.h"
#include "Resources/ResourceStateTracker.h"

class Resource;

namespace ui {

    class MemoryViewWidget {
    public:
        // Schedules a readback capture and opens the window.
        void Open(const std::string& passName, Resource* resource, const RangeSpec& range = {});

        // Draws the window if open.
        void Draw(bool* pOpen);

    private:
        void DrawBufferView(const ReadbackCaptureResult& r);
        void DrawTextureViewStub(const ReadbackCaptureResult& r);

    private:
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
    };

} // namespace ui
