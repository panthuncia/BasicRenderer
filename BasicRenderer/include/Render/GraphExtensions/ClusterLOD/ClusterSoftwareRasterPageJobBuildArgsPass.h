#pragma once

#include <array>
#include <memory>

#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Render/RenderContext.h"
#include "RenderPasses/Base/ComputePass.h"
#include "Resources/Buffers/Buffer.h"
#include "../../../../shaders/PerPassRootConstants/clodRasterizationRootConstants.h"

class ClusterSoftwareRasterPageJobBuildArgsPass : public ComputePass {
public:
    ClusterSoftwareRasterPageJobBuildArgsPass(
        std::shared_ptr<Buffer> rigidPageJobCountBuffer,
        std::shared_ptr<Buffer> rigidPageJobIndirectArgsBuffer,
        std::shared_ptr<Buffer> skinnedPageJobCountBuffer,
        std::shared_ptr<Buffer> skinnedPageJobIndirectArgsBuffer,
        bool runWhenComputeSWRasterEnabledOnly = false)
        : m_pageJobCountBuffers{ std::move(rigidPageJobCountBuffer), std::move(skinnedPageJobCountBuffer) }
        , m_pageJobIndirectArgsBuffers{ std::move(rigidPageJobIndirectArgsBuffer), std::move(skinnedPageJobIndirectArgsBuffer) }
        , m_runWhenComputeSWRasterEnabledOnly(runWhenComputeSWRasterEnabledOnly)
    {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"Shaders/ClusterLOD/softwareRasterPageJobs.hlsl",
            L"SWPageJobBuildIndirectArgsCSMain",
            {},
            "CLod_SoftwarePageJobBuildIndirectArgsPSO");
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) override
    {
        builder->WithShaderResource(m_pageJobCountBuffers[0], m_pageJobCountBuffers[1])
            .WithUnorderedAccess(m_pageJobIndirectArgsBuffers[0], m_pageJobIndirectArgsBuffers[1]);
    }

    void Setup() override {}

    void Update(const UpdateExecutionContext&) override
    {
        const RasterizeClustersCommand zeroArgs = {};
        for (const auto& pageJobIndirectArgsBuffer : m_pageJobIndirectArgsBuffers) {
            BUFFER_UPLOAD(&zeroArgs, sizeof(RasterizeClustersCommand), rg::runtime::UploadTarget::FromShared(pageJobIndirectArgsBuffer), 0);
        }
    }

    PassReturn Execute(PassExecutionContext& executionContext) override
    {
        if (m_runWhenComputeSWRasterEnabledOnly &&
            !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
            return {};
        }

        auto* renderContext = executionContext.hostData->Get<RenderContext>();
        auto& context = *renderContext;
        auto& commandList = executionContext.commandList;

        commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
        commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

        uint32_t misc[NumMiscUintRootConstants] = {};
        BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        for (uint32_t variantIndex = 0u; variantIndex < m_pageJobCountBuffers.size(); ++variantIndex) {
            misc[CLOD_RASTER_PAGE_JOB_COUNT_DESCRIPTOR_INDEX] = m_pageJobCountBuffers[variantIndex]->GetSRVInfo(0).slot.index;
            misc[CLOD_RASTER_PAGE_JOB_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_pageJobIndirectArgsBuffers[variantIndex]->GetUAVShaderVisibleInfo(0).slot.index;
            commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);
            commandList.Dispatch(1, 1, 1);
        }
        return {};
    }

    void Cleanup() override {}

private:
    PipelineState m_pso;
    std::array<std::shared_ptr<Buffer>, 2> m_pageJobCountBuffers;
    std::array<std::shared_ptr<Buffer>, 2> m_pageJobIndirectArgsBuffers;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
};
