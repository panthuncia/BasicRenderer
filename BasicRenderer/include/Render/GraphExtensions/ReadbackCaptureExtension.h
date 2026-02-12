#pragma once

#include "Render/RenderGraph.h"
#include "RenderPasses/ReadbackCapturePass.h"
#include "Managers/Singletons/ReadbackManager.h"

class ReadbackCaptureExtension final : public RenderGraph::IRenderGraphExtension {
public:
    void GatherStructuralPasses(RenderGraph&, std::vector<RenderGraph::ExternalPassDesc>&) override {
        // Readback capture is per-frame and ephemeral; we emit it via GatherFramePasses().
    }

    void GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& out) override {
        auto& manager = ReadbackManager::GetInstance();
        auto captures = manager.ConsumeCaptureRequests();

        uint32_t localIndex = 0;

        for (auto& capture : captures) {
            if (!capture.resource) {
                continue;
            }

            auto handle = rg.RequestResourceHandle(capture.resource, /*allowFailure=*/true);
            if (handle.GetGeneration() == 0) {
                continue;
            }

            RenderGraph::ExternalPassDesc desc{};
            desc.type = RenderGraph::PassType::Render;
            desc.where = RenderGraph::ExternalInsertPoint::After(capture.passName);
            desc.registerName = false;

            ReadbackCaptureInputs inputs{};
            inputs.target = ResourceHandleAndRange(handle, capture.range);

            auto pass = std::make_shared<ReadbackCapturePass>(inputs, std::move(capture.callback));
            desc.pass = pass;

            desc.name = "ReadbackCapture::" + capture.passName + "::" + std::to_string(handle.GetGlobalResourceID()) + "::" + std::to_string(localIndex++);
            out.push_back(std::move(desc));
        }
    }
};