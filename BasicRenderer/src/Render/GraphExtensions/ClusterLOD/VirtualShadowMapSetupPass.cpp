#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapSetupPass.h"

#include <array>
#include <cmath>

#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/RendererComponents.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowSetupRootConstants.h"

namespace {

std::array<CLodVirtualShadowClipmapInfo, CLodVirtualShadowDefaultClipmapCount> g_previousClipmapInfos{};
std::array<int64_t, CLodVirtualShadowDefaultClipmapCount> g_previousClipmapPageOffsetX{};
std::array<int64_t, CLodVirtualShadowDefaultClipmapCount> g_previousClipmapPageOffsetY{};
bool g_previousClipmapInfosValid = false;

uint32_t WrapPageOffset(int64_t pageCoord, uint32_t pageTableResolution)
{
    if (pageTableResolution == 0u) {
        return 0u;
    }

    const int64_t resolution = static_cast<int64_t>(pageTableResolution);
    int64_t wrapped = pageCoord % resolution;
    if (wrapped < 0) {
        wrapped += resolution;
    }

    return static_cast<uint32_t>(wrapped);
}

float ExtractOrthographicWidth(const DirectX::XMMATRIX& projection)
{
    const float m11 = projection.r[0].m128_f32[0];
    return std::abs(m11) > 1.0e-6f ? (2.0f / std::abs(m11)) : 0.0f;
}

float ExtractOrthographicHeight(const DirectX::XMMATRIX& projection)
{
    const float m22 = projection.r[1].m128_f32[1];
    return std::abs(m22) > 1.0e-6f ? (2.0f / std::abs(m22)) : 0.0f;
}

bool ClipmapStructureEquals(const CLodVirtualShadowClipmapInfo& lhs, const CLodVirtualShadowClipmapInfo& rhs)
{
    return lhs.texelWorldSize == rhs.texelWorldSize &&
        lhs.pageTableLayer == rhs.pageTableLayer &&
        lhs.shadowCameraBufferIndex == rhs.shadowCameraBufferIndex &&
        lhs.clipLevel == rhs.clipLevel &&
    ((lhs.flags & CLodVirtualShadowClipmapValidFlag) == (rhs.flags & CLodVirtualShadowClipmapValidFlag));
}

bool IsClipmapValid(const CLodVirtualShadowClipmapInfo& clipmapInfo)
{
    return (clipmapInfo.flags & CLodVirtualShadowClipmapValidFlag) != 0u &&
        clipmapInfo.shadowCameraBufferIndex != 0xFFFFFFFFu;
}

int32_t ClampClearOffset(int64_t delta, uint32_t pageTableResolution)
{
    if (pageTableResolution == 0u) {
        return 0;
    }

    const int64_t limit = static_cast<int64_t>(pageTableResolution);
    if (delta >= limit) {
        return static_cast<int32_t>(pageTableResolution);
    }
    if (delta <= -limit) {
        return -static_cast<int32_t>(pageTableResolution);
    }

    return static_cast<int32_t>(delta);
}

} // namespace

VirtualShadowMapSetupPass::VirtualShadowMapSetupPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> allocationCountBuffer,
    std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<Buffer> statsBuffer,
    std::shared_ptr<Buffer> runtimeStateBuffer,
    bool forceResetResources)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_allocationCountBuffer(std::move(allocationCountBuffer))
    , m_dirtyPageFlagsBuffer(std::move(dirtyPageFlagsBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_statsBuffer(std::move(statsBuffer))
    , m_runtimeStateBuffer(std::move(runtimeStateBuffer))
    , m_forceResetResources(forceResetResources)
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
    builder->WithShaderResource(m_clipmapInfoBuffer)
        .WithUnorderedAccess(
        m_pageTableTexture,
        m_pageMetadataBuffer,
        m_allocationCountBuffer,
        m_dirtyPageFlagsBuffer,
        m_statsBuffer);
}

void VirtualShadowMapSetupPass::Setup() {}

void VirtualShadowMapSetupPass::Update(const UpdateExecutionContext& executionContext)
{
    m_resetResources = m_forceResetResources || !g_previousClipmapInfosValid;

    CLodVirtualShadowRuntimeState runtimeState{};
    runtimeState.clipmapCount = CLodVirtualShadowDefaultClipmapCount;
    runtimeState.pageTableResolution = CLodVirtualShadowDefaultPageTableResolution;
    runtimeState.physicalPageCount = CLodVirtualShadowDefaultPhysicalPageCount;
    runtimeState.maxAllocationRequests = CLodVirtualShadowMaxAllocationRequests;
    BUFFER_UPLOAD(&runtimeState, sizeof(runtimeState), rg::runtime::UploadTarget::FromShared(m_runtimeStateBuffer), 0);

    std::array<CLodVirtualShadowClipmapInfo, CLodVirtualShadowDefaultClipmapCount> clipmapInfos{};

    auto* updateContext = executionContext.hostData ? executionContext.hostData->Get<UpdateContext>() : nullptr;
    if (updateContext && updateContext->viewManager) {
        auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
        auto lightQuery = ecsWorld.query_builder<const Components::Light, const Components::LightViewInfo>().build();

        bool foundDirectionalShadow = false;
        lightQuery.each([&](flecs::entity, const Components::Light& light, const Components::LightViewInfo& lightViewInfo) {
            if (foundDirectionalShadow || !light.lightInfo.shadowCaster || light.type != Components::LightType::Directional) {
                return;
            }

            foundDirectionalShadow = true;
            const uint32_t clipmapCount = std::min<uint32_t>(
                static_cast<uint32_t>(lightViewInfo.viewIDs.size()),
                CLodVirtualShadowDefaultClipmapCount);

            for (uint32_t clipmapIndex = 0; clipmapIndex < clipmapCount; ++clipmapIndex) {
                const View* view = updateContext->viewManager->Get(lightViewInfo.viewIDs[clipmapIndex]);
                if (!view) {
                    continue;
                }

                const float orthoWidth = ExtractOrthographicWidth(view->cameraInfo.unjitteredProjection);
                const float orthoHeight = ExtractOrthographicHeight(view->cameraInfo.unjitteredProjection);
                const float virtualShadowResolution = static_cast<float>(CLodVirtualShadowDefaultVirtualResolution);
                const float pageWorldSize =
                    std::max(orthoWidth, orthoHeight) /
                    std::max(static_cast<float>(CLodVirtualShadowDefaultPageTableResolution), 1.0f);

                const DirectX::XMVECTOR clipAnchorWorld = DirectX::XMVectorSet(
                    view->cameraInfo.positionWorldSpace.x,
                    view->cameraInfo.positionWorldSpace.y,
                    view->cameraInfo.positionWorldSpace.z,
                    1.0f);
                const DirectX::XMVECTOR clipAnchorLightView = DirectX::XMVector4Transform(clipAnchorWorld, view->cameraInfo.view);
                const float clipAnchorLightViewX = DirectX::XMVectorGetX(clipAnchorLightView);
                const float clipAnchorLightViewY = DirectX::XMVectorGetY(clipAnchorLightView);

                auto& clipmapInfo = clipmapInfos[clipmapIndex];
                const int64_t pageOffsetX = pageWorldSize > 1.0e-6f
                    ? static_cast<int64_t>(std::floor(clipAnchorLightViewX / pageWorldSize))
                    : 0;
                const int64_t pageOffsetY = pageWorldSize > 1.0e-6f
                    ? static_cast<int64_t>(std::floor(-clipAnchorLightViewY / pageWorldSize))
                    : 0;
                clipmapInfo.worldOriginX = view->cameraInfo.positionWorldSpace.x;
                clipmapInfo.worldOriginY = view->cameraInfo.positionWorldSpace.y;
                clipmapInfo.worldOriginZ = view->cameraInfo.positionWorldSpace.z;
                clipmapInfo.texelWorldSize = std::max(orthoWidth, orthoHeight) / std::max(virtualShadowResolution, 1.0f);
                clipmapInfo.pageOffsetX = WrapPageOffset(pageOffsetX, CLodVirtualShadowDefaultPageTableResolution);
                clipmapInfo.pageOffsetY = WrapPageOffset(pageOffsetY, CLodVirtualShadowDefaultPageTableResolution);
                clipmapInfo.pageTableLayer = clipmapIndex;
                clipmapInfo.shadowCameraBufferIndex = view->gpu.cameraBufferIndex;
                clipmapInfo.clipLevel = clipmapIndex;
                clipmapInfo.flags = CLodVirtualShadowClipmapValidFlag;
                if (g_previousClipmapInfosValid && IsClipmapValid(g_previousClipmapInfos[clipmapIndex])) {
                    clipmapInfo.clearOffsetX = ClampClearOffset(
                        pageOffsetX - g_previousClipmapPageOffsetX[clipmapIndex],
                        CLodVirtualShadowDefaultPageTableResolution);
                    clipmapInfo.clearOffsetY = ClampClearOffset(
                        pageOffsetY - g_previousClipmapPageOffsetY[clipmapIndex],
                        CLodVirtualShadowDefaultPageTableResolution);
                }
                g_previousClipmapPageOffsetX[clipmapIndex] = pageOffsetX;
                g_previousClipmapPageOffsetY[clipmapIndex] = pageOffsetY;
            }
        });
    }

    for (uint32_t clipmapIndex = 0; clipmapIndex < CLodVirtualShadowDefaultClipmapCount; ++clipmapIndex) {
        auto& info = clipmapInfos[clipmapIndex];
        info.pageTableLayer = clipmapIndex;
        if (info.shadowCameraBufferIndex == 0xFFFFFFFFu) {
            info.texelWorldSize = static_cast<float>(CLodVirtualShadowPhysicalPageSize << clipmapIndex);
        }

        if (!m_resetResources && !ClipmapStructureEquals(info, g_previousClipmapInfos[clipmapIndex])) {
            m_resetResources = true;
        }
    }

    g_previousClipmapInfos = clipmapInfos;
    g_previousClipmapInfosValid = true;

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
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_ALLOCATION_COUNT_DESCRIPTOR_INDEX] = m_allocationCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION] = CLodVirtualShadowDefaultPageTableResolution;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_CLIPMAP_COUNT] = CLodVirtualShadowDefaultClipmapCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PHYSICAL_PAGE_COUNT] = CLodVirtualShadowDefaultPhysicalPageCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_DIRTY_WORD_COUNT] = CLodVirtualShadowDirtyWordCount(CLodVirtualShadowDefaultPhysicalPageCount);
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_RESET_RESOURCES] = m_resetResources ? 1u : 0u;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;

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