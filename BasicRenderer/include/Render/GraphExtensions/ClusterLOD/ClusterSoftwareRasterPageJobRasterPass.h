#pragma once

#include <memory>

#include <rhi.h>

#include "BuiltinResources.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "RenderPasses/Base/ComputePass.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "../../../../shaders/PerPassRootConstants/clodRasterizationRootConstants.h"

class Buffer;
class ResourceGroup;

class ClusterSoftwareRasterPageJobRasterPass : public ComputePass {
public:
    ClusterSoftwareRasterPageJobRasterPass(
        std::shared_ptr<Buffer> compactedVisibleClustersBuffer,
        std::shared_ptr<Buffer> viewRasterInfoBuffer,
        std::shared_ptr<PixelBuffer> virtualShadowPageTableTexture,
        std::shared_ptr<PixelBuffer> virtualShadowPhysicalPagesTexture,
        std::shared_ptr<Buffer> virtualShadowClipmapInfoBuffer,
        std::shared_ptr<Buffer> pageJobCountBuffer,
        std::shared_ptr<Buffer> pageJobRecordsBuffer,
        std::shared_ptr<Buffer> pageJobIndirectArgsBuffer,
        std::shared_ptr<ResourceGroup> slabResourceGroup = nullptr,
        bool runWhenComputeSWRasterEnabledOnly = false)
        : m_compactedVisibleClustersBuffer(std::move(compactedVisibleClustersBuffer))
        , m_viewRasterInfoBuffer(std::move(viewRasterInfoBuffer))
        , m_virtualShadowPageTableTexture(std::move(virtualShadowPageTableTexture))
        , m_virtualShadowPhysicalPagesTexture(std::move(virtualShadowPhysicalPagesTexture))
        , m_virtualShadowClipmapInfoBuffer(std::move(virtualShadowClipmapInfoBuffer))
        , m_pageJobCountBuffer(std::move(pageJobCountBuffer))
        , m_pageJobRecordsBuffer(std::move(pageJobRecordsBuffer))
        , m_pageJobIndirectArgsBuffer(std::move(pageJobIndirectArgsBuffer))
        , m_slabResourceGroup(std::move(slabResourceGroup))
        , m_runWhenComputeSWRasterEnabledOnly(runWhenComputeSWRasterEnabledOnly)
    {
        rhi::IndirectArg args[] = {
            {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 3 } } },
            {.kind = rhi::IndirectArgKind::Dispatch }
        };

        auto device = DeviceManager::GetInstance().GetDevice();
        device.CreateCommandSignature(
            rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterizeClustersCommand) },
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            m_commandSignature);

        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"Shaders/ClusterLOD/softwareRasterPageJobs.hlsl",
            L"SWPageJobRasterPageCSMain",
            {},
            "CLod_SoftwarePageJobRasterPSO");
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) override
    {
        builder->WithShaderResource(
                Builtin::PerMeshBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerObjectBuffer,
                Builtin::CullingCameraBuffer,
                Builtin::SkeletonResources::InverseBindMatrices,
                Builtin::SkeletonResources::BoneTransforms,
                Builtin::SkeletonResources::SkinningInstanceInfo,
                m_compactedVisibleClustersBuffer,
                m_viewRasterInfoBuffer,
                m_virtualShadowClipmapInfoBuffer,
                m_pageJobCountBuffer,
                m_pageJobRecordsBuffer)
            .WithUnorderedAccess(m_virtualShadowPageTableTexture, m_virtualShadowPhysicalPagesTexture)
            .WithIndirectArguments(m_pageJobIndirectArgsBuffer)
            .WithConstantBuffer(Builtin::PerFrameBuffer);

        if (m_slabResourceGroup) {
            builder->WithShaderResource(ResourceGroupResolver(m_slabResourceGroup));
        }
    }

    void Setup() override {}

    PassReturn Execute(PassExecutionContext& executionContext) override
    {
        // if (m_runWhenComputeSWRasterEnabledOnly &&
        //     !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
        //     return {};
        // }

        // auto& settings = SettingsManager::GetInstance();
        // if (!settings.getSettingGetter<bool>(CLodEnablePageJobVSMSettingName)()) {
        //     return {};
        // }

        // auto* renderContext = executionContext.hostData->Get<RenderContext>();
        // auto& context = *renderContext;
        // auto& commandList = executionContext.commandList;

        // commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
        // commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

        // uint32_t misc[NumMiscUintRootConstants] = {};
        // misc[CLOD_RASTER_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedVisibleClustersBuffer->GetSRVInfo(0).slot.index;
        // misc[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX] = m_viewRasterInfoBuffer->GetSRVInfo(0).slot.index;
        // misc[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX] = m_virtualShadowPageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
        // misc[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_virtualShadowClipmapInfoBuffer->GetSRVInfo(0).slot.index;
        // misc[CLOD_RASTER_VIRTUAL_SHADOW_PHYSICAL_PAGES_DESCRIPTOR_INDEX] = m_virtualShadowPhysicalPagesTexture->GetUAVShaderVisibleInfo(0).slot.index;
        // misc[CLOD_RASTER_PAGE_JOB_COUNT_DESCRIPTOR_INDEX] = m_pageJobCountBuffer->GetSRVInfo(0).slot.index;
        // misc[CLOD_RASTER_PAGE_JOB_RECORDS_DESCRIPTOR_INDEX] = m_pageJobRecordsBuffer->GetSRVInfo(0).slot.index;

        // commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);
        // BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());
        // commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        // commandList.ExecuteIndirect(
        //     m_commandSignature->GetHandle(),
        //     m_pageJobIndirectArgsBuffer->GetAPIResource().GetHandle(),
        //     0,
        //     {},
        //     0,
        //     1);
        return {};
    }

    void Cleanup() override {}

private:
    PipelineState m_pso;
    rhi::CommandSignaturePtr m_commandSignature;
    std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
    std::shared_ptr<Buffer> m_viewRasterInfoBuffer;
    std::shared_ptr<PixelBuffer> m_virtualShadowPageTableTexture;
    std::shared_ptr<PixelBuffer> m_virtualShadowPhysicalPagesTexture;
    std::shared_ptr<Buffer> m_virtualShadowClipmapInfoBuffer;
    std::shared_ptr<Buffer> m_pageJobCountBuffer;
    std::shared_ptr<Buffer> m_pageJobRecordsBuffer;
    std::shared_ptr<Buffer> m_pageJobIndirectArgsBuffer;
    std::shared_ptr<ResourceGroup> m_slabResourceGroup;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
};