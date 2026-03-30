#pragma once

#include <spdlog/spdlog.h>

#include "Render/RenderGraph/RenderGraph.h"
#include "RenderPasses/ReadbackCapturePass.h"
#include "RenderPasses/ReadbackCopyCapturePass.h"
#include "Render/Runtime/IReadbackService.h"

class ReadbackCaptureExtension final : public RenderGraph::IRenderGraphExtension {
public:
    explicit ReadbackCaptureExtension(rg::runtime::IReadbackService* readbackService)
        : m_readbackService(readbackService) {
    }

    void GatherStructuralPasses(RenderGraph&, std::vector<RenderGraph::ExternalPassDesc>&) override {
        // Readback capture is per-frame and ephemeral; we emit it via GatherFramePasses().
    }

    void GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& out) override {
        if (!m_readbackService) {
            return;
        }

        auto captures = m_readbackService->ConsumeCaptureRequests();

        uint32_t localIndex = 0;

        for (auto& capture : captures) {
            QueueKind preferredQueueKind = capture.preferredQueueKind;
            if (preferredQueueKind != QueueKind::Graphics && preferredQueueKind != QueueKind::Copy) {
                spdlog::warn(
                    "ReadbackCaptureExtension: capture for pass '{}' requested unsupported queue kind {}; falling back to graphics.",
                    capture.passName,
                    static_cast<int>(preferredQueueKind));
                preferredQueueKind = QueueKind::Graphics;
            }

            auto resource = capture.resource.lock();
            if (!resource && capture.resourceId != 0) {
                resource = rg.GetResourceByID(capture.resourceId);
            }

            if (!resource) {
                spdlog::warn(
                    "ReadbackCaptureExtension: dropping capture for pass '{}' because resource id {} is no longer available.",
                    capture.passName,
                    capture.resourceId);
                continue;
            }

            auto handle = rg.RequestResourceHandle(resource.get(), /*allowFailure=*/true);
            if (handle.GetGeneration() == 0) {
                spdlog::warn(
                    "ReadbackCaptureExtension: failed to resolve handle for capture resource id {} after pass '{}'.",
                    capture.resourceId,
                    capture.passName);
                continue;
            }

            RenderGraph::ExternalPassDesc desc{};
            desc.where = RenderGraph::ExternalInsertPoint::After(capture.passName);
            desc.registerName = false;

            if (preferredQueueKind == QueueKind::Copy) {
                // Route through copy-queue CopyPass for lower latency
                desc.type = RenderGraph::PassType::Copy;
                desc.preferredQueueKind = QueueKind::Copy;

                ReadbackCopyCaptureInputs inputs{};
                inputs.target = ResourceHandleAndRange(handle, capture.range);

                auto pass = std::make_shared<ReadbackCopyCapturePass>(inputs, std::move(capture.callback), m_readbackService);
                desc.pass = pass;
            }
            else {
                // Default: graphics-queue RenderPass (existing path)
                desc.type = RenderGraph::PassType::Render;

                ReadbackCaptureInputs inputs{};
                inputs.target = ResourceHandleAndRange(handle, capture.range);

                auto pass = std::make_shared<ReadbackCapturePass>(inputs, std::move(capture.callback), m_readbackService);
                desc.pass = pass;
            }

            desc.name = "ReadbackCapture::" + capture.passName + "::" + std::to_string(handle.GetGlobalResourceID()) + "::" + std::to_string(localIndex++);
            out.push_back(std::move(desc));
        }
    }

private:
    rg::runtime::IReadbackService* m_readbackService = nullptr; // non-owning
};