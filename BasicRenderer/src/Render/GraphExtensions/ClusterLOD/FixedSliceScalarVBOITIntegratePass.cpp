#include "Render/GraphExtensions/ClusterLOD/FixedSliceScalarVBOITIntegratePass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Managers/ViewManager.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "../shaders/PerPassRootConstants/clodFixedSliceScalarVBOITIntegrateRootConstants.h"
#include "../shaders/PerPassRootConstants/visUtilRootConstants.h"

FixedSliceScalarVBOITIntegratePass::FixedSliceScalarVBOITIntegratePass(
    std::shared_ptr<Buffer> reyesDiceQueueBuffer,
    std::shared_ptr<Buffer> reyesTessTableConfigsBuffer,
    std::shared_ptr<Buffer> reyesTessTableVerticesBuffer,
    std::shared_ptr<Buffer> reyesTessTableTrianglesBuffer,
    std::shared_ptr<Buffer> deepVisibilityNodesBuffer,
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<PixelBuffer> occupancyTexture,
    std::shared_ptr<PixelBuffer> extinctionTexture,
    std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
    uint32_t patchVisibilityIndexBase)
    : m_reyesDiceQueueBuffer(std::move(reyesDiceQueueBuffer))
    , m_reyesTessTableConfigsBuffer(std::move(reyesTessTableConfigsBuffer))
    , m_reyesTessTableVerticesBuffer(std::move(reyesTessTableVerticesBuffer))
    , m_reyesTessTableTrianglesBuffer(std::move(reyesTessTableTrianglesBuffer))
    , m_deepVisibilityNodesBuffer(std::move(deepVisibilityNodesBuffer))
    , m_configBuffer(std::move(configBuffer))
    , m_occupancyTexture(std::move(occupancyTexture))
    , m_extinctionTexture(std::move(extinctionTexture))
    , m_integratedTransmittanceTexture(std::move(integratedTransmittanceTexture))
    , m_patchVisibilityIndexBase(patchVisibilityIndexBase)
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/FixedSliceScalarVBOITIntegrate.hlsl",
        L"CLodFixedSliceScalarVBOITIntegrateCS",
        {},
        "CLod.FixedSliceScalarVBOITIntegrate.PSO");
}

void FixedSliceScalarVBOITIntegratePass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(
            Builtin::PostSkinningVertices,
            Builtin::PerObjectBuffer,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::CameraBuffer,
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
            m_deepVisibilityNodesBuffer,
            m_configBuffer);

    if (m_reyesDiceQueueBuffer) {
        builder->WithShaderResource(m_reyesDiceQueueBuffer);
    }

    if (m_reyesTessTableConfigsBuffer && m_reyesTessTableVerticesBuffer && m_reyesTessTableTrianglesBuffer) {
        builder->WithShaderResource(
            m_reyesTessTableConfigsBuffer,
            m_reyesTessTableVerticesBuffer,
            m_reyesTessTableTrianglesBuffer);
    }

    if (m_primaryHeadPointerTexture) {
        builder->WithShaderResource(m_primaryHeadPointerTexture);
    }

    builder->WithUnorderedAccess(
        Builtin::DebugVisualization,
        m_occupancyTexture,
        m_extinctionTexture,
        m_integratedTransmittanceTexture);

    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
}

void FixedSliceScalarVBOITIntegratePass::Setup()
{
}

void FixedSliceScalarVBOITIntegratePass::Update(const UpdateExecutionContext& executionContext)
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

bool FixedSliceScalarVBOITIntegratePass::DeclaredResourcesChanged() const
{
    return m_declaredResourcesChanged;
}

PassReturn FixedSliceScalarVBOITIntegratePass::Execute(PassExecutionContext& executionContext)
{
    if (!m_primaryHeadPointerTexture || !m_configBuffer) {
        return {};
    }

    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t misc[NumMiscUintRootConstants] = {};
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_HEAD_POINTER_DESCRIPTOR_INDEX] = m_primaryHeadPointerTexture->GetSRVInfo(0).slot.index;
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_NODE_BUFFER_DESCRIPTOR_INDEX] = m_deepVisibilityNodesBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_CONFIG_DESCRIPTOR_INDEX] = m_configBuffer->GetSRVInfo(0).slot.index;
    misc[VISBUF_REYES_DICE_QUEUE_DESCRIPTOR_INDEX] = m_reyesDiceQueueBuffer
        ? m_reyesDiceQueueBuffer->GetSRVInfo(0).slot.index
        : 0xFFFFFFFFu;
    misc[VISBUF_REYES_PATCH_INDEX_BASE] = m_patchVisibilityIndexBase;
    misc[VISBUF_REYES_TESS_TABLE_CONFIGS_DESCRIPTOR_INDEX] = m_reyesTessTableConfigsBuffer
        ? m_reyesTessTableConfigsBuffer->GetSRVInfo(0).slot.index
        : 0xFFFFFFFFu;
    misc[VISBUF_REYES_TESS_TABLE_VERTICES_DESCRIPTOR_INDEX] = m_reyesTessTableVerticesBuffer
        ? m_reyesTessTableVerticesBuffer->GetSRVInfo(0).slot.index
        : 0xFFFFFFFFu;
    misc[VISBUF_REYES_TESS_TABLE_TRIANGLES_DESCRIPTOR_INDEX] = m_reyesTessTableTrianglesBuffer
        ? m_reyesTessTableTrianglesBuffer->GetSRVInfo(0).slot.index
        : 0xFFFFFFFFu;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        misc);

    const uint32_t groupCountX = (m_occupancyTexture->GetWidth() + 7u) / 8u;
    const uint32_t groupCountY = (m_occupancyTexture->GetHeight() + 7u) / 8u;
    commandList.Dispatch(groupCountX, groupCountY, 1u);
    return {};
}

void FixedSliceScalarVBOITIntegratePass::Cleanup()
{
}