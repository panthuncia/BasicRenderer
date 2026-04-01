#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapSetupPass.h"

#include <array>

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowSetupRootConstants.h"

VirtualShadowMapSetupPass::VirtualShadowMapSetupPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> allocationCountBuffer,
    std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<Buffer> runtimeStateBuffer)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_allocationCountBuffer(std::move(allocationCountBuffer))
    , m_dirtyPageFlagsBuffer(std::move(dirtyPageFlagsBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_runtimeStateBuffer(std::move(runtimeStateBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowSetupCSMain",
        {},
        "CLod.VirtualShadow.Setup.PSO");
}

void VirtualShadowMapSetupPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithUnorderedAccess(
        m_pageTableTexture,
        m_pageMetadataBuffer,
    m_allocationCountBuffer,
        m_dirtyPageFlagsBuffer);
}

void VirtualShadowMapSetupPass::Setup() {}

void VirtualShadowMapSetupPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;

    CLodVirtualShadowRuntimeState runtimeState{};
    runtimeState.clipmapCount = CLodVirtualShadowDefaultClipmapCount;
    runtimeState.pageTableResolution = CLodVirtualShadowDefaultPageTableResolution;
    runtimeState.physicalPageCount = CLodVirtualShadowDefaultPhysicalPageCount;
    runtimeState.maxAllocationRequests = CLodVirtualShadowMaxAllocationRequests;
    BUFFER_UPLOAD(&runtimeState, sizeof(runtimeState), rg::runtime::UploadTarget::FromShared(m_runtimeStateBuffer), 0);

    std::array<CLodVirtualShadowClipmapInfo, CLodVirtualShadowDefaultClipmapCount> clipmapInfos{};
    for (uint32_t clipmapIndex = 0; clipmapIndex < CLodVirtualShadowDefaultClipmapCount; ++clipmapIndex) {
        auto& info = clipmapInfos[clipmapIndex];
        info.texelWorldSize = static_cast<float>(CLodVirtualShadowPhysicalPageSize << clipmapIndex);
        info.pageTableLayer = clipmapIndex;
        info.shadowCameraBufferIndex = 0xFFFFFFFFu;
    }

    BUFFER_UPLOAD(
        clipmapInfos.data(),
        static_cast<uint32_t>(clipmapInfos.size() * sizeof(CLodVirtualShadowClipmapInfo)),
        rg::runtime::UploadTarget::FromShared(m_clipmapInfoBuffer),
        0);
}

PassReturn VirtualShadowMapSetupPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_ALLOCATION_COUNT_DESCRIPTOR_INDEX] = m_allocationCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION] = CLodVirtualShadowDefaultPageTableResolution;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_CLIPMAP_COUNT] = CLodVirtualShadowDefaultClipmapCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PHYSICAL_PAGE_COUNT] = CLodVirtualShadowDefaultPhysicalPageCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_DIRTY_WORD_COUNT] = CLodVirtualShadowDirtyWordCount(CLodVirtualShadowDefaultPhysicalPageCount);

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    constexpr uint32_t kThreadsPerDimension = 8u;
    const uint32_t groupCountX = (CLodVirtualShadowDefaultPageTableResolution + kThreadsPerDimension - 1u) / kThreadsPerDimension;
    const uint32_t groupCountY = (CLodVirtualShadowDefaultPageTableResolution + kThreadsPerDimension - 1u) / kThreadsPerDimension;
    commandList.Dispatch(groupCountX, groupCountY, CLodVirtualShadowDefaultClipmapCount);

    return {};
}

void VirtualShadowMapSetupPass::Cleanup() {}