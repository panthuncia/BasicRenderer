#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include <rhi.h>

#include "BuiltinResources.h"
#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "RenderPasses/Base/ComputePass.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "../../../../shaders/PerPassRootConstants/clodVirtualShadowBlockExpandRootConstants.h"

class Buffer;
class ResourceGroup;

enum class VirtualShadowBlockExpandMode : uint8_t
{
    Histogram,
    Emit,
};

class VirtualShadowBlockExpandPass : public ComputePass {
public:
    VirtualShadowBlockExpandPass(
        VirtualShadowBlockExpandMode mode,
        std::shared_ptr<Buffer> sourceVisibleClustersBuffer,
        std::shared_ptr<Buffer> sourceHistogramBuffer,
        std::shared_ptr<Buffer> sourceIndirectArgsBuffer,
        std::shared_ptr<Buffer> expandedHistogramBuffer,
        std::shared_ptr<Buffer> expandedOffsetsBuffer,
        std::shared_ptr<Buffer> expandedWriteCursorBuffer,
        std::shared_ptr<Buffer> expandedVisibleClustersBuffer,
        std::shared_ptr<Buffer> expandedBlockMetaBuffer,
        std::shared_ptr<PixelBuffer> virtualShadowPageTableTexture,
        std::shared_ptr<Buffer> virtualShadowClipmapInfoBuffer,
        uint32_t expandedRecordCapacity,
        uint32_t blockSoftCap,
        std::shared_ptr<ResourceGroup> slabResourceGroup = nullptr,
        bool runWhenComputeSWRasterEnabledOnly = false)
        : m_mode(mode)
        , m_sourceVisibleClustersBuffer(std::move(sourceVisibleClustersBuffer))
        , m_sourceHistogramBuffer(std::move(sourceHistogramBuffer))
        , m_sourceIndirectArgsBuffer(std::move(sourceIndirectArgsBuffer))
        , m_expandedHistogramBuffer(std::move(expandedHistogramBuffer))
        , m_expandedOffsetsBuffer(std::move(expandedOffsetsBuffer))
        , m_expandedWriteCursorBuffer(std::move(expandedWriteCursorBuffer))
        , m_expandedVisibleClustersBuffer(std::move(expandedVisibleClustersBuffer))
        , m_expandedBlockMetaBuffer(std::move(expandedBlockMetaBuffer))
        , m_virtualShadowPageTableTexture(std::move(virtualShadowPageTableTexture))
        , m_virtualShadowClipmapInfoBuffer(std::move(virtualShadowClipmapInfoBuffer))
        , m_expandedRecordCapacity(expandedRecordCapacity)
        , m_blockSoftCap(std::min(blockSoftCap, CLodVirtualShadowBlockMaxTrackedPerCluster))
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

        const wchar_t* entryPoint = mode == VirtualShadowBlockExpandMode::Histogram
            ? L"CLodVirtualShadowBlockHistogramCSMain"
            : L"CLodVirtualShadowBlockEmitCSMain";
        const char* rigidName = mode == VirtualShadowBlockExpandMode::Histogram
            ? "CLod_VirtualShadowBlockHistogramPSO"
            : "CLod_VirtualShadowBlockEmitPSO";
        const char* skinnedName = mode == VirtualShadowBlockExpandMode::Histogram
            ? "CLod_VirtualShadowBlockHistogramSkinnedPSO"
            : "CLod_VirtualShadowBlockEmitSkinnedPSO";

        m_rigidPso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"Shaders/ClusterLOD/virtualShadowBlockExpansion.hlsl",
            entryPoint,
            {},
            rigidName);
        std::vector<DxcDefine> skinnedDefines = { DxcDefine{ L"PSO_SKINNED", L"1" } };
        m_skinnedPso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"Shaders/ClusterLOD/virtualShadowBlockExpansion.hlsl",
            entryPoint,
            skinnedDefines,
            skinnedName);
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) override
    {
        builder->WithShaderResource(
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerObjectBuffer,
                Builtin::CullingCameraBuffer,
                Builtin::SkeletonResources::InverseBindMatrices,
                Builtin::SkeletonResources::BoneTransforms,
                Builtin::SkeletonResources::SkinningInstanceInfo,
                m_sourceVisibleClustersBuffer,
                m_sourceHistogramBuffer,
                m_virtualShadowClipmapInfoBuffer)
            .WithUnorderedAccess(
                m_virtualShadowPageTableTexture,
                m_expandedHistogramBuffer)
            .WithIndirectArguments(m_sourceIndirectArgsBuffer)
            .WithConstantBuffer(Builtin::PerFrameBuffer);

        if (m_mode == VirtualShadowBlockExpandMode::Emit) {
            builder->WithShaderResource(m_expandedOffsetsBuffer)
                .WithUnorderedAccess(
                    m_expandedWriteCursorBuffer,
                    m_expandedVisibleClustersBuffer,
                    m_expandedBlockMetaBuffer);
        }

        if (m_slabResourceGroup) {
            builder->WithShaderResource(ResourceGroupResolver(m_slabResourceGroup));
        }
    }

    void Setup() override {}

    void Update(const UpdateExecutionContext& executionContext) override
    {
        if (m_runWhenComputeSWRasterEnabledOnly &&
            !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
            return;
        }

        auto* updateContext = executionContext.hostData->Get<UpdateContext>();
        auto& context = *updateContext;
        const uint32_t numBuckets = context.materialManager->GetRasterBucketCount();

        std::vector<uint32_t> zeroBuckets(numBuckets, 0u);
        if (m_mode == VirtualShadowBlockExpandMode::Histogram) {
            if (m_expandedHistogramBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
                m_expandedHistogramBuffer->ResizeStructured(numBuckets);
            }

            BUFFER_UPLOAD(
                zeroBuckets.data(),
                static_cast<uint32_t>(zeroBuckets.size() * sizeof(uint32_t)),
                rg::runtime::UploadTarget::FromShared(m_expandedHistogramBuffer),
                0);
        }

        if (m_mode == VirtualShadowBlockExpandMode::Emit) {
            if (m_expandedWriteCursorBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
                m_expandedWriteCursorBuffer->ResizeStructured(numBuckets);
            }

            BUFFER_UPLOAD(
                zeroBuckets.data(),
                static_cast<uint32_t>(zeroBuckets.size() * sizeof(uint32_t)),
                rg::runtime::UploadTarget::FromShared(m_expandedWriteCursorBuffer),
                0);
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
        misc[CLOD_VSM_BLOCK_EXPAND_SOURCE_HISTOGRAM_DESCRIPTOR_INDEX] = m_sourceHistogramBuffer->GetSRVInfo(0).slot.index;
        misc[CLOD_VSM_BLOCK_EXPAND_SOURCE_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_sourceVisibleClustersBuffer->GetSRVInfo(0).slot.index;
        misc[CLOD_VSM_BLOCK_EXPAND_EXPANDED_HISTOGRAM_DESCRIPTOR_INDEX] = m_expandedHistogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        misc[CLOD_VSM_BLOCK_EXPAND_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX] =
            m_virtualShadowPageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
        misc[CLOD_VSM_BLOCK_EXPAND_VIRTUAL_SHADOW_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_virtualShadowClipmapInfoBuffer->GetSRVInfo(0).slot.index;
        misc[CLOD_VSM_BLOCK_EXPAND_RECORD_CAPACITY] = m_expandedRecordCapacity;
        misc[CLOD_VSM_BLOCK_EXPAND_BLOCK_SOFT_CAP] = m_blockSoftCap;

        if (m_mode == VirtualShadowBlockExpandMode::Emit) {
            misc[CLOD_VSM_BLOCK_EXPAND_EXPANDED_OFFSETS_DESCRIPTOR_INDEX] = m_expandedOffsetsBuffer->GetSRVInfo(0).slot.index;
            misc[CLOD_VSM_BLOCK_EXPAND_EXPANDED_WRITE_CURSOR_DESCRIPTOR_INDEX] = m_expandedWriteCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            misc[CLOD_VSM_BLOCK_EXPAND_EXPANDED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_expandedVisibleClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            misc[CLOD_VSM_BLOCK_EXPAND_EXPANDED_BLOCK_META_DESCRIPTOR_INDEX] = m_expandedBlockMetaBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        }

        const uint32_t numBuckets = context.materialManager->GetRasterBucketCount();
        if (numBuckets == 0u) {
            return {};
        }

        const auto apiResource = m_sourceIndirectArgsBuffer->GetAPIResource();
        const uint64_t stride = sizeof(RasterizeClustersCommand);
        for (uint32_t bucketIndex = 0u; bucketIndex < numBuckets; ++bucketIndex) {
            const MaterialRasterFlags flags = context.materialManager->GetRasterFlagsForBucket(bucketIndex);
            const PipelineState& pso = (flags & MaterialRasterFlagsSkinned) ? m_skinnedPso : m_rigidPso;

            commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);
            BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
            commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

            const uint64_t argOffset = static_cast<uint64_t>(bucketIndex) * stride;
            commandList.ExecuteIndirect(
                m_commandSignature->GetHandle(),
                apiResource.GetHandle(),
                argOffset,
                {},
                0,
                1);
        }

        return {};
    }

    void Cleanup() override {}

private:
    VirtualShadowBlockExpandMode m_mode = VirtualShadowBlockExpandMode::Histogram;
    PipelineState m_rigidPso;
    PipelineState m_skinnedPso;
    rhi::CommandSignaturePtr m_commandSignature;
    std::shared_ptr<Buffer> m_sourceVisibleClustersBuffer;
    std::shared_ptr<Buffer> m_sourceHistogramBuffer;
    std::shared_ptr<Buffer> m_sourceIndirectArgsBuffer;
    std::shared_ptr<Buffer> m_expandedHistogramBuffer;
    std::shared_ptr<Buffer> m_expandedOffsetsBuffer;
    std::shared_ptr<Buffer> m_expandedWriteCursorBuffer;
    std::shared_ptr<Buffer> m_expandedVisibleClustersBuffer;
    std::shared_ptr<Buffer> m_expandedBlockMetaBuffer;
    std::shared_ptr<PixelBuffer> m_virtualShadowPageTableTexture;
    std::shared_ptr<Buffer> m_virtualShadowClipmapInfoBuffer;
    uint32_t m_expandedRecordCapacity = 0u;
    uint32_t m_blockSoftCap = 1u;
    std::shared_ptr<ResourceGroup> m_slabResourceGroup;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
};
