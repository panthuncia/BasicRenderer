#include "Render/GraphExtensions/ClusterLOD/DeepVisibilityResolvePass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "../shaders/PerPassRootConstants/clodDeepVisibilityResolveRootConstants.h"
#include "../shaders/PerPassRootConstants/visUtilRootConstants.h"

DeepVisibilityResolvePass::DeepVisibilityResolvePass(
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> deepVisibilityNodesBuffer,
    std::shared_ptr<Buffer> deepVisibilityCounterBuffer,
    std::shared_ptr<Buffer> deepVisibilityOverflowCounterBuffer,
    std::shared_ptr<Buffer> deepVisibilityStatsBuffer)
    : m_visibleClustersBuffer(std::move(visibleClustersBuffer))
    , m_deepVisibilityNodesBuffer(std::move(deepVisibilityNodesBuffer))
    , m_deepVisibilityCounterBuffer(std::move(deepVisibilityCounterBuffer))
    , m_deepVisibilityOverflowCounterBuffer(std::move(deepVisibilityOverflowCounterBuffer))
    , m_deepVisibilityStatsBuffer(std::move(deepVisibilityStatsBuffer))
{
    auto& settingsManager = SettingsManager::GetInstance();
    m_getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
    m_getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
    m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();
}

void DeepVisibilityResolvePass::DeclareResourceUsages(RenderPassBuilder* builder)
{
    builder->WithShaderResource(
            Builtin::Light::BufferGroup,
            Builtin::PostSkinningVertices,
            Builtin::PerObjectBuffer,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::Environment::PrefilteredCubemapsGroup,
            Builtin::Environment::InfoBuffer,
            Builtin::CameraBuffer,
            Builtin::Light::ActiveLightIndices,
            Builtin::Light::InfoBuffer,
            Builtin::Light::PointLightCubemapBuffer,
            Builtin::Light::SpotLightMatrixBuffer,
            Builtin::Light::DirectionalLightCascadeBuffer,
            Builtin::Light::ClusterBuffer,
            Builtin::Light::PagesBuffer,
            //Builtin::Shadows::ShadowMaps,
            Builtin::CLod::Offsets,
            Builtin::CLod::GroupChunks,
            Builtin::CLod::Groups,
            Builtin::CLod::MeshMetadata,
            Builtin::MeshResources::MeshletTriangles,
            Builtin::MeshResources::MeshletVertexIndices,
            Builtin::MeshResources::MeshletOffsets,
            Builtin::SkeletonResources::InverseBindMatrices,
            Builtin::SkeletonResources::BoneTransforms,
            Builtin::SkeletonResources::SkinningInstanceInfo,
            m_visibleClustersBuffer,
            m_deepVisibilityNodesBuffer,
            m_deepVisibilityCounterBuffer,
            m_deepVisibilityOverflowCounterBuffer)
        .WithRenderTarget(Builtin::Color::HDRColorTarget)
        .WithUnorderedAccess(Builtin::DebugVisualization)
        .WithUnorderedAccess(m_deepVisibilityStatsBuffer);

    if (m_primaryHeadPointerTexture) {
        builder->WithShaderResource(m_primaryHeadPointerTexture);
    }
}

void DeepVisibilityResolvePass::Setup()
{
    m_pHDRTarget = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Color::HDRColorTarget);

    RegisterSRV(Builtin::Light::ActiveLightIndices);
    RegisterSRV(Builtin::Light::InfoBuffer);
    RegisterSRV(Builtin::Light::PointLightCubemapBuffer);
    RegisterSRV(Builtin::Light::SpotLightMatrixBuffer);
    RegisterSRV(Builtin::Light::DirectionalLightCascadeBuffer);
    RegisterSRV(Builtin::Light::ClusterBuffer);
    RegisterSRV(Builtin::Light::PagesBuffer);
    RegisterSRV(Builtin::Environment::InfoBuffer);
    RegisterSRV(Builtin::CameraBuffer);
    //RegisterSRV(Builtin::Shadows::ShadowMaps);
    RegisterSRV(Builtin::PostSkinningVertices);
    RegisterSRV(Builtin::PerObjectBuffer);
    RegisterSRV(Builtin::NormalMatrixBuffer);
    RegisterSRV(Builtin::PerMeshBuffer);
    RegisterSRV(Builtin::PerMeshInstanceBuffer);
    RegisterSRV(Builtin::PerMaterialDataBuffer);
    RegisterSRV(Builtin::CLod::Offsets);
    RegisterSRV(Builtin::CLod::GroupChunks);
    RegisterSRV(Builtin::CLod::Groups);
    RegisterSRV(Builtin::CLod::MeshMetadata);
    RegisterSRV(Builtin::MeshResources::MeshletTriangles);
    RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
    RegisterSRV(Builtin::MeshResources::MeshletOffsets);
    RegisterSRV(Builtin::SkeletonResources::InverseBindMatrices);
    RegisterSRV(Builtin::SkeletonResources::BoneTransforms);
    RegisterSRV(Builtin::SkeletonResources::SkinningInstanceInfo);

    RegisterUAV(Builtin::DebugVisualization);
}

void DeepVisibilityResolvePass::Update(const UpdateExecutionContext& executionContext)
{
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;

    std::shared_ptr<PixelBuffer> primaryHeadPointers;
    context.viewManager->ForEachFiltered(ViewFilter::PrimaryCameras(), [&](uint64_t viewID) {
        if (!primaryHeadPointers) {
            primaryHeadPointers = context.viewManager->EnsureCLodDeepVisibilityHeadPointers(viewID);
        }
    });

    m_declaredResourcesChanged = m_primaryHeadPointerTexture != primaryHeadPointers;
    m_primaryHeadPointerTexture = std::move(primaryHeadPointers);
}

bool DeepVisibilityResolvePass::DeclaredResourcesChanged() const
{
    return m_declaredResourcesChanged;
}

PassReturn DeepVisibilityResolvePass::Execute(PassExecutionContext& executionContext)
{
    if (!m_primaryHeadPointerTexture || !m_pHDRTarget) {
        return {};
    }

    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;
    auto& psoManager = PSOManager::GetInstance();

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

    rhi::PassBeginInfo passInfo{};
    rhi::ColorAttachment colorAttachment{};
    colorAttachment.rtv = m_pHDRTarget->GetRTVInfo(0).slot;
    colorAttachment.loadOp = rhi::LoadOp::Load;
    colorAttachment.storeOp = rhi::StoreOp::Store;
    passInfo.colors = { &colorAttachment };
    passInfo.width = context.renderResolution.x;
    passInfo.height = context.renderResolution.y;
    passInfo.debugName = "CLod alpha deep visibility resolve";
    commandList.BeginPass(passInfo);

    commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleStrip);
    commandList.BindLayout(psoManager.GetRootSignature().GetHandle());

    const auto& pso = psoManager.GetClusterLODDeepVisibilityResolvePSO(context.globalPSOFlags);
    commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());

    unsigned int settings[NumSettingsRootConstants] = {};
    settings[EnableShadows] = m_getShadowsEnabled();
    settings[EnablePunctualLights] = m_getPunctualLightingEnabled();
    settings[EnableGTAO] = m_gtaoEnabled;
    commandList.PushConstants(
        rhi::ShaderStage::AllGraphics,
        0,
        SettingsRootSignatureIndex,
        0,
        NumSettingsRootConstants,
        settings);

    uint32_t misc[NumMiscUintRootConstants] = {};
    misc[CLOD_DEEP_VISIBILITY_RESOLVE_HEAD_POINTER_DESCRIPTOR_INDEX] = m_primaryHeadPointerTexture->GetSRVInfo(0).slot.index;
    misc[CLOD_DEEP_VISIBILITY_RESOLVE_NODE_BUFFER_DESCRIPTOR_INDEX] = m_deepVisibilityNodesBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_DEEP_VISIBILITY_RESOLVE_COUNTER_DESCRIPTOR_INDEX] = m_deepVisibilityCounterBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_DEEP_VISIBILITY_RESOLVE_OVERFLOW_COUNTER_DESCRIPTOR_INDEX] = m_deepVisibilityOverflowCounterBuffer->GetSRVInfo(0).slot.index;
    misc[VISBUF_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_DEEP_VISIBILITY_RESOLVE_STATS_DESCRIPTOR_INDEX] = m_deepVisibilityStatsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    commandList.PushConstants(
        rhi::ShaderStage::AllGraphics,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        misc);

    commandList.Draw(3, 1, 0, 0);
    return {};
}

void DeepVisibilityResolvePass::Cleanup()
{
}
