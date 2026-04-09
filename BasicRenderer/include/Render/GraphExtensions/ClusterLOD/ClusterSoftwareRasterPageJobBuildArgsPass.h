#pragma once

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
        std::shared_ptr<Buffer> pageJobCountBuffer,
        std::shared_ptr<Buffer> pageJobIndirectArgsBuffer,
        bool runWhenComputeSWRasterEnabledOnly = false)
        : m_pageJobCountBuffer(std::move(pageJobCountBuffer))
        , m_pageJobIndirectArgsBuffer(std::move(pageJobIndirectArgsBuffer))
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
        builder->WithShaderResource(m_pageJobCountBuffer)
            .WithUnorderedAccess(m_pageJobIndirectArgsBuffer);
    }

    void Setup() override {}

    void Update(const UpdateExecutionContext&) override
    {
        const RasterizeClustersCommand zeroArgs = {};
        BUFFER_UPLOAD(&zeroArgs, sizeof(RasterizeClustersCommand), rg::runtime::UploadTarget::FromShared(m_pageJobIndirectArgsBuffer), 0);
    }

    PassReturn Execute(PassExecutionContext& executionContext) override
    {
        // if (m_runWhenComputeSWRasterEnabledOnly &&
        //     !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
        //     return {};
        // }

        // auto* renderContext = executionContext.hostData->Get<RenderContext>();
        // auto& context = *renderContext;
        // auto& commandList = executionContext.commandList;

        // commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
        // commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

        // uint32_t misc[NumMiscUintRootConstants] = {};
        // misc[CLOD_RASTER_PAGE_JOB_COUNT_DESCRIPTOR_INDEX] = m_pageJobCountBuffer->GetSRVInfo(0).slot.index;
        // misc[CLOD_RASTER_PAGE_JOB_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_pageJobIndirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;

        // commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);
        // BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());
        // commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        // commandList.Dispatch(1, 1, 1);
        return {};
    }

    void Cleanup() override {}

private:
    PipelineState m_pso;
    std::shared_ptr<Buffer> m_pageJobCountBuffer;
    std::shared_ptr<Buffer> m_pageJobIndirectArgsBuffer;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
};