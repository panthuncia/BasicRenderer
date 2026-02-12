#include "Menu/MemoryViewWidget.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstddef>

#include "Managers/Singletons/ReadbackManager.h"
#include "Resources/Resource.h"

namespace ui {

    static inline char ToPrintableAscii(std::byte b) {
        unsigned char c = (unsigned char)b;
        if (c >= 32 && c <= 126) return (char)c;
        return '.';
    }

    void MemoryViewWidget::Open(const std::string& passName, Resource* resource, const RangeSpec& range) {
        std::scoped_lock lock(mutex_);

        result_.reset();
        waiting_ = false;
        status_.clear();

        if (!resource || passName.empty()) {
            status_ = "Missing pass or resource selection.";
            return;
        }

        PendingRequest req{};
        req.passName = passName;
        req.resource = resource;
        req.range = range;
        req.resourceId = resource->GetGlobalResourceID();
        req.resourceName = resource->GetName();
        pending_ = req;

        waiting_ = true;
        status_ = "Scheduling readback...";

        // Schedule capture. Callback may run later when GPU completes + ProcessReadbackRequests() copies data.
        ReadbackManager::GetInstance().RequestReadbackCapture(
            passName,
            resource,
            range,
            [this](ReadbackCaptureResult&& r) {
                std::scoped_lock cbLock(mutex_);
                result_ = std::move(r);
                waiting_ = false;
                status_ = "Readback complete.";
            });
    }

    void MemoryViewWidget::Draw(bool* pOpen) {
        if (!pOpen || !*pOpen) return;

        if (!ImGui::Begin("Memory View", pOpen)) {
            ImGui::End();
            return;
        }

        PendingRequest pendingCopy{};
        bool havePending = false;
        std::optional<ReadbackCaptureResult> resultCopy;
        bool waiting = false;
        std::string status;

        {
            std::scoped_lock lock(mutex_);
            if (pending_) {
                pendingCopy = *pending_;
                havePending = true;
            }
            resultCopy = result_;
            waiting = waiting_;
            status = status_;
        }

        if (havePending) {
            ImGui::Text("Pass: %s", pendingCopy.passName.c_str());
            ImGui::Text("Resource: %s [%llu]", pendingCopy.resourceName.empty() ? "(unnamed)" : pendingCopy.resourceName.c_str(),
                (unsigned long long)pendingCopy.resourceId);
        }
        else {
            ImGui::TextUnformatted("No capture requested.");
        }

        if (!status.empty()) {
            if (waiting) ImGui::TextDisabled("%s", status.c_str());
            else ImGui::TextUnformatted(status.c_str());
        }

        ImGui::Separator();

        if (!resultCopy.has_value()) {
            ImGui::TextUnformatted(waiting ? "Waiting for GPU readback..." : "No data yet.");
            ImGui::End();
            return;
        }

        const ReadbackCaptureResult& r = *resultCopy;

        if (ImGui::BeginTabBar("##MemoryViewTabs")) {
            if (ImGui::BeginTabItem("Buffer")) {
                if (r.desc.kind == ReadbackResourceKind::Buffer) {
                    DrawBufferView(r);
                }
                else {
                    ImGui::TextUnformatted("The captured resource is a texture.");
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Texture")) {
                if (r.desc.kind == ReadbackResourceKind::Texture) {
                    DrawTextureViewStub(r);
                }
                else {
                    ImGui::TextUnformatted("The captured resource is a buffer.");
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void MemoryViewWidget::DrawTextureViewStub(const ReadbackCaptureResult& r) {
        ImGui::TextUnformatted("Texture view is stubbed out for now.");
        ImGui::Separator();
        ImGui::Text("Format: %d", (int)r.format);
        ImGui::Text("Dimensions: %ux%u (depth %u)", r.width, r.height, r.depth);
        ImGui::Text("Footprints: %d", (int)r.layouts.size());
        ImGui::Text("Data size: %llu bytes", (unsigned long long)r.data.size());
    }

    void MemoryViewWidget::DrawBufferView(const ReadbackCaptureResult& r) {
        const size_t sizeBytes = r.data.size();
        ImGui::Text("Size: %llu bytes", (unsigned long long)sizeBytes);

        int bpr = bytesPerRow_;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Bytes/row", &bpr)) {
            bpr = std::clamp(bpr, 4, 64);
            bytesPerRow_ = bpr;
        }

        if (sizeBytes == 0) {
            ImGui::TextUnformatted("(empty)");
            return;
        }

        const int bytesPerRow = std::clamp(bytesPerRow_, 4, 64);
        const int rowCount = (int)((sizeBytes + (size_t)bytesPerRow - 1) / (size_t)bytesPerRow);

        ImGui::BeginChild("##HexView", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        ImGuiListClipper clip;
        clip.Begin(rowCount);
        while (clip.Step()) {
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                const size_t base = (size_t)row * (size_t)bytesPerRow;

                char line[512];
                int n = 0;

                n += std::snprintf(line + n, sizeof(line) - n, "%08llX  ", (unsigned long long)base);

                // Hex bytes
                for (int i = 0; i < bytesPerRow; ++i) {
                    const size_t idx = base + (size_t)i;
                    if (idx < sizeBytes) {
                        const unsigned v = (unsigned)std::to_integer<unsigned char>(r.data[idx]);
                        n += std::snprintf(line + n, sizeof(line) - n, "%02X ", v);
                    }
                    else {
                        n += std::snprintf(line + n, sizeof(line) - n, "   ");
                    }
                }

                // ASCII
                n += std::snprintf(line + n, sizeof(line) - n, " |");
                for (int i = 0; i < bytesPerRow; ++i) {
                    const size_t idx = base + (size_t)i;
                    if (idx < sizeBytes) {
                        line[n++] = ToPrintableAscii(r.data[idx]);
                    }
                    else {
                        line[n++] = ' ';
                    }
                }
                line[n++] = '|';
                line[n++] = 0;

                ImGui::TextUnformatted(line);
            }
        }

        ImGui::EndChild();
    }

} // namespace ui
