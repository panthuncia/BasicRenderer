#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapSetupPass.h"

#include <array>
#include <cmath>

#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/RendererComponents.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowSetupRootConstants.h"

namespace {

std::array<CLodVirtualShadowClipmapInfo, CLodVirtualShadowMaxSupportedClipmapCount> g_previousClipmapInfos{};
std::array<int64_t, CLodVirtualShadowMaxSupportedClipmapCount> g_previousClipmapPageOffsetX{};
std::array<int64_t, CLodVirtualShadowMaxSupportedClipmapCount> g_previousClipmapPageOffsetY{};
bool g_previousClipmapInfosValid = false;
DirectX::XMFLOAT3 g_previousDirectionalLightDirection{};
bool g_previousDirectionalLightDirectionValid = false;

uint32_t GetVirtualShadowVirtualResolution()
{
    return CLodVirtualShadowSanitizeVirtualResolution(
        SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodVirtualShadowVirtualResolutionSettingName)());
}

uint32_t GetVirtualShadowPageTableResolution()
{
    return CLodVirtualShadowPageTableResolutionFromVirtualResolution(GetVirtualShadowVirtualResolution());
}

uint32_t GetVirtualShadowPhysicalPagesPerAxis()
{
    return CLodVirtualShadowSanitizePhysicalPagesPerAxis(
        SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodVirtualShadowPhysicalPagesPerAxisSettingName)());
}

uint32_t GetVirtualShadowPhysicalPageCount()
{
    return CLodVirtualShadowPhysicalPageCountFromPagesPerAxis(GetVirtualShadowPhysicalPagesPerAxis());
}

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

bool NearlyEqualFloat(float lhs, float rhs, float epsilon = 1.0e-5f)
{
    return std::abs(lhs - rhs) <= epsilon * std::max(std::max(std::abs(lhs), std::abs(rhs)), 1.0f);
}

bool NearlyEqualDirection(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs, float cosineThreshold = 0.9999f)
{
    const float dot = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    return dot >= cosineThreshold;
}

bool ClipmapStructureEquals(const CLodVirtualShadowClipmapInfo& lhs, const CLodVirtualShadowClipmapInfo& rhs)
{
    return NearlyEqualFloat(lhs.texelWorldSize, rhs.texelWorldSize) &&
        lhs.pageTableLayer == rhs.pageTableLayer &&
        lhs.clipLevel == rhs.clipLevel &&
        lhs.virtualResolution == rhs.virtualResolution &&
        lhs.pageTableResolution == rhs.pageTableResolution &&
        lhs.physicalPagesPerAxis == rhs.physicalPagesPerAxis &&
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
    std::shared_ptr<Buffer> markClipmapDataBuffer,
    std::shared_ptr<Buffer> compactMainCameraBuffer,
    std::shared_ptr<Buffer> compactShadowCameraBuffer,
    std::shared_ptr<Buffer> statsBuffer,
    std::shared_ptr<Buffer> runtimeStateBuffer,
    std::shared_ptr<Buffer> predictiveCandidateCountBuffer,
    std::shared_ptr<Buffer> predictiveRawPageCountBuffer,
    std::shared_ptr<Buffer> predictedPageCountBuffer,
    bool forceResetResources)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_allocationCountBuffer(std::move(allocationCountBuffer))
    , m_dirtyPageFlagsBuffer(std::move(dirtyPageFlagsBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_markClipmapDataBuffer(std::move(markClipmapDataBuffer))
    , m_compactMainCameraBuffer(std::move(compactMainCameraBuffer))
    , m_compactShadowCameraBuffer(std::move(compactShadowCameraBuffer))
    , m_statsBuffer(std::move(statsBuffer))
    , m_runtimeStateBuffer(std::move(runtimeStateBuffer))
    , m_predictiveCandidateCountBuffer(std::move(predictiveCandidateCountBuffer))
    , m_predictiveRawPageCountBuffer(std::move(predictiveRawPageCountBuffer))
    , m_predictedPageCountBuffer(std::move(predictedPageCountBuffer))
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
        m_markClipmapDataBuffer,
        m_compactMainCameraBuffer,
        m_compactShadowCameraBuffer,
        m_statsBuffer,
        m_predictiveCandidateCountBuffer,
        m_predictiveRawPageCountBuffer,
        m_predictedPageCountBuffer);

    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
}

void VirtualShadowMapSetupPass::Setup() {}

void VirtualShadowMapSetupPass::Update(const UpdateExecutionContext& executionContext)
{
    const bool disableVirtualShadowPageCaching =
        SettingsManager::GetInstance().getSettingGetter<bool>(CLodDisableVirtualShadowPageCachingSettingName)();
    const bool forceResetResources = m_forceResetResources || disableVirtualShadowPageCaching;
    m_forceResetResources = false;
    const uint32_t virtualShadowResolution = GetVirtualShadowVirtualResolution();
    const uint32_t virtualShadowPageTableResolution = GetVirtualShadowPageTableResolution();
    const uint32_t virtualShadowPhysicalPagesPerAxis = GetVirtualShadowPhysicalPagesPerAxis();
    const uint32_t virtualShadowPhysicalPageCount = GetVirtualShadowPhysicalPageCount();

    m_resetReasonForced = forceResetResources;
    m_resetReasonNoPreviousState = !g_previousClipmapInfosValid;
    m_resetReasonStructureMismatch = false;
    m_resetReasonLightDirectionChanged = false;
    m_resetResources = m_resetReasonForced || m_resetReasonNoPreviousState;

    CLodVirtualShadowRuntimeState runtimeState{};
    std::array<CLodVirtualShadowClipmapInfo, CLodVirtualShadowMaxSupportedClipmapCount> clipmapInfos{};
    std::array<CLodVirtualShadowMarkClipmapData, CLodVirtualShadowMaxSupportedClipmapCount> markClipmapData{};
    std::array<CLodVirtualShadowCompactShadowCameraInfo, CLodVirtualShadowMaxSupportedClipmapCount> compactShadowCameras{};
    CLodVirtualShadowMainCameraInfo compactMainCamera{};
    DirectX::XMFLOAT3 currentDirectionalLightDirection{};
    bool currentDirectionalLightDirectionValid = false;
    uint32_t activeClipmapCount = 0u;

    auto* updateContext = executionContext.hostData ? executionContext.hostData->Get<UpdateContext>() : nullptr;
    if (updateContext && updateContext->viewManager) {
        bool foundPrimaryCamera = false;
        updateContext->viewManager->ForEachFiltered(ViewFilter::PrimaryCameras(), [&](uint64_t viewId) {
            if (foundPrimaryCamera) {
                return;
            }

            const View* view = updateContext->viewManager->Get(viewId);
            if (!view) {
                return;
            }

            foundPrimaryCamera = true;
            compactMainCamera.positionWorldSpace = view->cameraInfo.positionWorldSpace;
            compactMainCamera.viewInverse = view->cameraInfo.viewInverse;
            compactMainCamera.projectionInverse = view->cameraInfo.projectionInverse;
        });

        auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
        auto lightQuery = ecsWorld.query_builder<const Components::Light, const Components::LightViewInfo>().build();

        bool foundDirectionalShadow = false;
        lightQuery.each([&](flecs::entity, const Components::Light& light, const Components::LightViewInfo& lightViewInfo) {
            if (foundDirectionalShadow || !light.lightInfo.shadowCaster || light.type != Components::LightType::Directional) {
                return;
            }

            foundDirectionalShadow = true;
            DirectX::XMStoreFloat3(
                &currentDirectionalLightDirection,
                DirectX::XMVector3Normalize(light.lightInfo.dirWorldSpace));
            currentDirectionalLightDirectionValid = true;
            const uint32_t clipmapCount = std::min<uint32_t>(
                static_cast<uint32_t>(lightViewInfo.viewIDs.size()),
                CLodVirtualShadowMaxSupportedClipmapCount);
            activeClipmapCount = clipmapCount;

            for (uint32_t clipmapIndex = 0; clipmapIndex < clipmapCount; ++clipmapIndex) {
                const View* view = updateContext->viewManager->Get(lightViewInfo.viewIDs[clipmapIndex]);
                if (!view) {
                    continue;
                }

                const float orthoWidth = ExtractOrthographicWidth(view->cameraInfo.unjitteredProjection);
                const float orthoHeight = ExtractOrthographicHeight(view->cameraInfo.unjitteredProjection);
                const float virtualShadowResolutionFloat = static_cast<float>(virtualShadowResolution);

                auto& clipmapInfo = clipmapInfos[clipmapIndex];
                const int64_t pageOffsetX =
                    clipmapIndex < lightViewInfo.virtualShadowUnwrappedPageOffsetX.size()
                    ? lightViewInfo.virtualShadowUnwrappedPageOffsetX[clipmapIndex]
                    : 0;
                const int64_t pageOffsetY =
                    clipmapIndex < lightViewInfo.virtualShadowUnwrappedPageOffsetY.size()
                    ? lightViewInfo.virtualShadowUnwrappedPageOffsetY[clipmapIndex]
                    : 0;
                clipmapInfo.worldOriginX = view->cameraInfo.positionWorldSpace.x;
                clipmapInfo.worldOriginY = view->cameraInfo.positionWorldSpace.y;
                clipmapInfo.worldOriginZ = view->cameraInfo.positionWorldSpace.z;
                clipmapInfo.texelWorldSize = std::max(orthoWidth, orthoHeight) / std::max(virtualShadowResolutionFloat, 1.0f);
                clipmapInfo.pageOffsetX = WrapPageOffset(pageOffsetX, virtualShadowPageTableResolution);
                clipmapInfo.pageOffsetY = WrapPageOffset(pageOffsetY, virtualShadowPageTableResolution);
                clipmapInfo.pageTableLayer = clipmapIndex;
                clipmapInfo.shadowCameraBufferIndex = view->gpu.cameraBufferIndex;
                clipmapInfo.clipLevel = clipmapIndex;
                clipmapInfo.flags = CLodVirtualShadowClipmapValidFlag;
                clipmapInfo.virtualResolution = virtualShadowResolution;
                clipmapInfo.pageTableResolution = virtualShadowPageTableResolution;
                clipmapInfo.physicalPagesPerAxis = virtualShadowPhysicalPagesPerAxis;

                auto& markData = markClipmapData[clipmapIndex];
                markData.texelWorldSize = clipmapInfo.texelWorldSize;
                markData.pageOffsetX = clipmapInfo.pageOffsetX;
                markData.pageOffsetY = clipmapInfo.pageOffsetY;
                markData.pageTableLayer = clipmapInfo.pageTableLayer;
                markData.flags = clipmapInfo.flags;
                markData.virtualResolution = clipmapInfo.virtualResolution;
                markData.pageTableResolution = clipmapInfo.pageTableResolution;
                markData.physicalPagesPerAxis = clipmapInfo.physicalPagesPerAxis;
                markData.directionalPageViewRow = DirectX::XMFLOAT4(
                    view->cameraInfo.view.r[3].m128_f32[0],
                    view->cameraInfo.view.r[3].m128_f32[1],
                    view->cameraInfo.view.r[3].m128_f32[2],
                    view->cameraInfo.view.r[3].m128_f32[3]);
                markData.shadowViewProjection = view->cameraInfo.viewProjection;

                compactShadowCameras[clipmapIndex].view = view->cameraInfo.view;
                compactShadowCameras[clipmapIndex].projection = view->cameraInfo.jitteredProjection;
                compactShadowCameras[clipmapIndex].viewProjection = view->cameraInfo.viewProjection;
                compactShadowCameras[clipmapIndex].isOrtho = view->cameraInfo.isOrtho;
                if (g_previousClipmapInfosValid && IsClipmapValid(g_previousClipmapInfos[clipmapIndex])) {
                    clipmapInfo.clearOffsetX = ClampClearOffset(
                        pageOffsetX - g_previousClipmapPageOffsetX[clipmapIndex],
                        virtualShadowPageTableResolution);
                    clipmapInfo.clearOffsetY = ClampClearOffset(
                        pageOffsetY - g_previousClipmapPageOffsetY[clipmapIndex],
                        virtualShadowPageTableResolution);
                }
                g_previousClipmapPageOffsetX[clipmapIndex] = pageOffsetX;
                g_previousClipmapPageOffsetY[clipmapIndex] = pageOffsetY;
            }
        });
    }

    if (currentDirectionalLightDirectionValid &&
        g_previousDirectionalLightDirectionValid &&
        !NearlyEqualDirection(currentDirectionalLightDirection, g_previousDirectionalLightDirection)) {
        m_resetReasonLightDirectionChanged = true;
        m_resetResources = true;
    }

    for (uint32_t clipmapIndex = 0; clipmapIndex < CLodVirtualShadowMaxSupportedClipmapCount; ++clipmapIndex) {
        auto& info = clipmapInfos[clipmapIndex];
        auto& markData = markClipmapData[clipmapIndex];
        info.pageTableLayer = clipmapIndex;
        info.virtualResolution = virtualShadowResolution;
        info.pageTableResolution = virtualShadowPageTableResolution;
        info.physicalPagesPerAxis = virtualShadowPhysicalPagesPerAxis;
        if (info.shadowCameraBufferIndex == 0xFFFFFFFFu) {
            info.texelWorldSize = static_cast<float>(CLodVirtualShadowPhysicalPageSize << clipmapIndex);
        }

        markData.texelWorldSize = info.texelWorldSize;
        markData.pageOffsetX = info.pageOffsetX;
        markData.pageOffsetY = info.pageOffsetY;
        markData.pageTableLayer = info.pageTableLayer;
        markData.flags = info.flags;
        markData.virtualResolution = info.virtualResolution;
        markData.pageTableResolution = info.pageTableResolution;
        markData.physicalPagesPerAxis = info.physicalPagesPerAxis;

        if (!m_resetResources && !ClipmapStructureEquals(info, g_previousClipmapInfos[clipmapIndex])) {
            m_resetReasonStructureMismatch = true;
            m_resetResources = true;
        }
    }

    g_previousClipmapInfos = clipmapInfos;
    g_previousClipmapInfosValid = true;
    g_previousDirectionalLightDirection = currentDirectionalLightDirection;
    g_previousDirectionalLightDirectionValid = currentDirectionalLightDirectionValid;

    runtimeState.clipmapCount = activeClipmapCount;
    runtimeState.supportedClipmapCount = CLodVirtualShadowMaxSupportedClipmapCount;
    runtimeState.virtualResolution = virtualShadowResolution;
    runtimeState.pageTableResolution = virtualShadowPageTableResolution;
    runtimeState.physicalPagesPerAxis = virtualShadowPhysicalPagesPerAxis;
    runtimeState.physicalPageCount = virtualShadowPhysicalPageCount;
    runtimeState.maxAllocationRequests = (std::min)(virtualShadowPhysicalPageCount, CLodVirtualShadowMaxAllocationRequests);
    BUFFER_UPLOAD(&runtimeState, sizeof(runtimeState), rg::runtime::UploadTarget::FromShared(m_runtimeStateBuffer), 0);

    BUFFER_UPLOAD(
        clipmapInfos.data(),
        static_cast<uint32_t>(clipmapInfos.size() * sizeof(CLodVirtualShadowClipmapInfo)),
        rg::runtime::UploadTarget::FromShared(m_clipmapInfoBuffer),
        0);

    BUFFER_UPLOAD(
        markClipmapData.data(),
        static_cast<uint32_t>(markClipmapData.size() * sizeof(CLodVirtualShadowMarkClipmapData)),
        rg::runtime::UploadTarget::FromShared(m_markClipmapDataBuffer),
        0);

    BUFFER_UPLOAD(
        &compactMainCamera,
        sizeof(compactMainCamera),
        rg::runtime::UploadTarget::FromShared(m_compactMainCameraBuffer),
        0);

    BUFFER_UPLOAD(
        compactShadowCameras.data(),
        static_cast<uint32_t>(compactShadowCameras.size() * sizeof(CLodVirtualShadowCompactShadowCameraInfo)),
        rg::runtime::UploadTarget::FromShared(m_compactShadowCameraBuffer),
        0);
}

PassReturn VirtualShadowMapSetupPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;
    const uint32_t virtualShadowPageTableResolution = GetVirtualShadowPageTableResolution();
    const uint32_t virtualShadowPhysicalPageCount = GetVirtualShadowPhysicalPageCount();

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_ALLOCATION_COUNT_DESCRIPTOR_INDEX] = m_allocationCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION] = virtualShadowPageTableResolution;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PHYSICAL_PAGE_COUNT] = virtualShadowPhysicalPageCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_DIRTY_WORD_COUNT] = CLodVirtualShadowDirtyWordCount(virtualShadowPhysicalPageCount);
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_RESET_RESOURCES] = m_resetResources ? 1u : 0u;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_RESET_REASON_FORCED] = m_resetReasonForced ? 1u : 0u;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_RESET_REASON_NO_PREVIOUS_STATE] = m_resetReasonNoPreviousState ? 1u : 0u;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_RESET_REASON_STRUCTURE_MISMATCH] = m_resetReasonStructureMismatch ? 1u : 0u;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_RESET_REASON_LIGHT_DIRECTION_CHANGED] = m_resetReasonLightDirectionChanged ? 1u : 0u;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PREDICTIVE_CANDIDATE_COUNT_DESCRIPTOR_INDEX] = m_predictiveCandidateCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PREDICTIVE_RAW_PAGE_COUNT_DESCRIPTOR_INDEX] = m_predictiveRawPageCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_SETUP_PREDICTED_PAGE_COUNT_DESCRIPTOR_INDEX] = m_predictedPageCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    constexpr uint32_t kThreadsPerDimension = 8u;
    const uint32_t groupCountX = (virtualShadowPageTableResolution + kThreadsPerDimension - 1u) / kThreadsPerDimension;
    const uint32_t groupCountY = (virtualShadowPageTableResolution + kThreadsPerDimension - 1u) / kThreadsPerDimension;
    commandList.Dispatch(groupCountX, groupCountY, CLodVirtualShadowMaxSupportedClipmapCount);

    return {};
}

void VirtualShadowMapSetupPass::Cleanup() {}