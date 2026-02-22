#pragma once

#include "Render/RenderGraph/RenderGraph.h"
#include "RenderPasses/ReadbackCapturePass.h"
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
            auto resource = capture.resource.lock();
            if (!resource) {
                continue;
            }

            auto handle = rg.RequestResourceHandle(resource.get(), /*allowFailure=*/true);
            if (handle.GetGeneration() == 0) {
                continue;
            }

            RenderGraph::ExternalPassDesc desc{};
            desc.type = RenderGraph::PassType::Render;
            desc.where = RenderGraph::ExternalInsertPoint::After(capture.passName);
            desc.registerName = false;

            ReadbackCaptureInputs inputs{};
            inputs.target = ResourceHandleAndRange(handle, capture.range);

            auto pass = std::make_shared<ReadbackCapturePass>(inputs, std::move(capture.callback), m_readbackService);
            desc.pass = pass;

            desc.name = "ReadbackCapture::" + capture.passName + "::" + std::to_string(handle.GetGlobalResourceID()) + "::" + std::to_string(localIndex++);
            out.push_back(std::move(desc));
        }
    }

private:
    rg::runtime::IReadbackService* m_readbackService = nullptr; // non-owning
};