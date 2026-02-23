#pragma once

#include <algorithm>
#include <unordered_set>

#include "RenderPasses/Base/RenderPass.h"
#include "Render/RenderContext.h"
#include "Managers/ViewManager.h"

class LinearDepthHistoryCopyPass : public RenderPass {
public:
    LinearDepthHistoryCopyPass() = default;

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        builder->WithCopySource(
            "Builtin::PrimaryCamera::LinearDepthMap",
            "Builtin::Shadows::LinearShadowMaps")
            .WithCopyDest(Builtin::LastFrameLinearDepthMaps);
    }

    void Setup() override {
    }

    PassReturn Execute(PassExecutionContext& executionContext) override {
        auto* renderContext = executionContext.hostData->Get<RenderContext>();
        auto& context = *renderContext;
        auto& commandList = executionContext.commandList;

        std::unordered_set<uint64_t> copiedSources;

        context.viewManager->ForEachView([&](uint64_t viewID) {
            auto* view = context.viewManager->Get(viewID);
            if (!view || !view->gpu.linearDepthMap || !view->gpu.lastFrameLinearDepthMap) {
                return;
            }
            
            const auto& source = view->gpu.linearDepthMap;
            const auto& history = view->gpu.lastFrameLinearDepthMap;
            const uint64_t sourceID = source->GetGlobalResourceID();
            if (copiedSources.contains(sourceID)) {
                return;
            }

            copiedSources.insert(sourceID);

            const auto& desc = source->GetDescription();
            const uint32_t sliceCount = desc.isCubemap
                ? 6u * (std::max)(1u, desc.arraySize)
                : (desc.isArray ? (std::max)(1u, desc.arraySize) : 1u);
            const uint32_t mipCount = source->GetNumUAVMipLevels();

            for (uint32_t slice = 0; slice < sliceCount; ++slice) {
                for (uint32_t mip = 0; mip < mipCount; ++mip) {
                    rhi::TextureCopyRegion srcRegion = {};
                    srcRegion.texture = source->GetAPIResource().GetHandle();
                    srcRegion.mip = mip;
                    srcRegion.arraySlice = slice;
                    srcRegion.x = 0;
                    srcRegion.y = 0;
                    srcRegion.z = 0;
                    srcRegion.width = (std::max)(1u, source->GetInternalWidth() >> mip);
                    srcRegion.height = (std::max)(1u, source->GetInternalHeight() >> mip);
                    srcRegion.depth = 1;

                    rhi::TextureCopyRegion dstRegion = {};
                    dstRegion.texture = history->GetAPIResource().GetHandle();
                    dstRegion.mip = mip;
                    dstRegion.arraySlice = slice;
                    dstRegion.x = 0;
                    dstRegion.y = 0;
                    dstRegion.z = 0;
                    dstRegion.width = srcRegion.width;
                    dstRegion.height = srcRegion.height;
                    dstRegion.depth = srcRegion.depth;

                    commandList.CopyTextureRegion(dstRegion, srcRegion);
                }
            }
        });

        return {};
    }

    void Cleanup() override {
    }
};
