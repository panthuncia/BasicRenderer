#pragma once

#include <rhi.h>
#include <rhi_interop_dx12.h>
#include <cstring>

#include "Render/RenderGraph/RenderGraph.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Resources/Buffers/Buffer.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodRootConstants.h"

struct RasterBucketsHistogramIndirectCommand
{
    unsigned int clusterCount;
    unsigned int dispatchXDimension;
    unsigned int dispatchX, dispatchY, dispatchZ;
};

struct RasterizeClustersCommand
{
    unsigned int baseClusterOffset;
    unsigned int xDim;
    unsigned int rasterBucketID;
    unsigned int dispatchX, dispatchY, dispatchZ;
};

struct CLodViewRasterInfo
{
    uint32_t visibilityUAVDescriptorIndex;
    uint32_t scissorMinX;
    uint32_t scissorMinY;
    uint32_t scissorMaxX;
    uint32_t scissorMaxY;
    float viewportScaleX;
    float viewportScaleY;
    uint32_t pad0;

    friend bool operator==(const CLodViewRasterInfo&, const CLodViewRasterInfo&) = default;
};

static constexpr uint32_t CLodReplayBufferSizeBytes = 8u * 1024u * 1024u;
static constexpr uint32_t CLodReplayBufferNumUints = CLodReplayBufferSizeBytes / sizeof(uint32_t);
static constexpr uint32_t CLodMaxViewDepthIndices = 512u;

inline std::shared_ptr<Buffer> CreateAliasedUnmaterializedStructuredBuffer(
    uint32_t numElements,
    uint32_t elementSize,
    bool unorderedAccess = true,
    bool unorderedAccessCounter = false,
    bool createNonShaderVisibleUAV = false,
    bool allowAlias = true)
{
    auto buffer = Buffer::CreateUnmaterializedStructuredBuffer(
        numElements,
        elementSize,
        unorderedAccess,
        unorderedAccessCounter,
        createNonShaderVisibleUAV,
        rhi::HeapType::DeviceLocal);
    buffer->SetAllowAlias(allowAlias);
    return buffer;
}

class CLodExtension final : public RenderGraph::IRenderGraphExtension {
public:
    explicit CLodExtension(CLodExtensionType type, uint64_t maxVisibleClusters) : m_maxVisibleClusters(maxVisibleClusters) {

        // Initialize global entity for extension type
        auto ecsWorld = ECSManager::GetInstance().GetWorld();
        // TODO: Better way to do this? Weird global initialization
        switch (type) {
        case CLodExtensionType::VisiblityBuffer:
            if (ecsWorld.component<CLodExtensionVisibilityBufferTag>().has(flecs::Exclusive)) {
                // Already initialized
                break;
            }
            ecsWorld.component<CLodExtensionVisibilityBufferTag>().add(flecs::Exclusive);
            ecsWorld.add<CLodExtensionVisibilityBufferTag>();
            break;
        case CLodExtensionType::Shadow:
            if (ecsWorld.component<CLodExtensionShadowTag>().has(flecs::Exclusive)) {
                // Already initialized
                break;
            }
            ecsWorld.component<CLodExtensionShadowTag>().add(flecs::Exclusive);
            ecsWorld.add<CLodExtensionShadowTag>();
            break;
        }

        m_visibleClustersBuffer = CreateAliasedUnmaterializedStructuredBuffer(static_cast<uint32_t>(maxVisibleClusters), sizeof(VisibleCluster), true, false);
        m_visibleClustersBuffer->SetName("CLod Visible Clusters Buffer (uncompacted)");
        m_histogramIndirectCommand = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterBucketsHistogramIndirectCommand), true, false);
        m_histogramIndirectCommand->SetName("CLod Raster Buckets Histogram Indirect Command Buffer");
        m_rasterBucketsHistogramBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_rasterBucketsHistogramBuffer->SetName("Raster bucket histogram");

        flecs::entity typeEntity;
        switch (type) {
        case CLodExtensionType::VisiblityBuffer:
            typeEntity = ecsWorld.entity<CLodExtensionVisibilityBufferTag>();
            break;
        case CLodExtensionType::Shadow:
            typeEntity = ecsWorld.entity<CLodExtensionShadowTag>();
            break;
        }

        m_visibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
        m_visibleClustersCounterBuffer->SetName("CLod Visible Clusters Counter Buffer");
        m_visibleClustersCounterBuffer->GetECSEntity()
            .set<Components::Resource>({ m_visibleClustersCounterBuffer })
            .add<VisibleClustersCounterTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_workGraphTelemetryBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodWorkGraphCounterCount, sizeof(uint32_t), true, false, false, false);
        m_workGraphTelemetryBuffer->SetName("CLod Work Graph Telemetry Buffer");
        m_workGraphTelemetryBuffer->GetECSEntity()
            .set<Components::Resource>({ m_workGraphTelemetryBuffer })
            .add<CLodWorkGraphTelemetryBufferTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_occlusionReplayBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodReplayBufferNumUints, sizeof(uint32_t), true, false, false, false);
        m_occlusionReplayBuffer->SetName("CLod Occlusion Replay Buffer");
        m_occlusionReplayStateBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReplayBufferState), true, false, false, false);
        m_occlusionReplayStateBuffer->SetName("CLod Occlusion Replay State Buffer");
        m_occlusionNodeGpuInputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(3, sizeof(CLodNodeGpuInput), true, false, false, false);
        m_occlusionNodeGpuInputsBuffer->SetName("CLod Occlusion Node GPU Inputs Buffer");
        m_viewDepthSrvIndicesBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodMaxViewDepthIndices, sizeof(CLodViewDepthSRVIndex), true, false, false, false);
        m_viewDepthSrvIndicesBuffer->SetName("CLod View Depth SRV Indices Buffer");

        m_rasterBucketsOffsetsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
        m_rasterBucketsOffsetsBuffer->SetName("CLod Raster bucket offsets");
        m_rasterBucketsBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
        m_rasterBucketsBlockSumsBuffer->SetName("CLod Raster bucket block sums");
        m_rasterBucketsScannedBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
        m_rasterBucketsScannedBlockSumsBuffer->SetName("CLod Raster bucket scanned block sums");
        m_rasterBucketsTotalCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
        m_rasterBucketsTotalCountBuffer->SetName("CLod Raster bucket total count");

        m_compactedVisibleClustersBuffer = CreateAliasedUnmaterializedStructuredBuffer(static_cast<uint32_t>(maxVisibleClusters), sizeof(VisibleCluster), true, false);
        m_compactedVisibleClustersBuffer->SetName("CLod Compacted Visible Clusters Buffer");
        // This tags the buffer with the extension type so passes can query for it with ECSResourceResolver
        m_compactedVisibleClustersBuffer->GetECSEntity()
            .set<Components::Resource>({ m_compactedVisibleClustersBuffer })
            .add<VisibleClustersBufferTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_rasterBucketsWriteCursorBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_rasterBucketsWriteCursorBuffer->SetName("CLod Raster bucket write cursor");
        m_rasterBucketsIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false);
        m_rasterBucketsIndirectArgsBuffer->SetName("CLod Raster bucket indirect args");

        m_visibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
        m_visibleClustersCounterBufferPhase2->SetName("CLod Visible Clusters Counter Buffer Phase2");

        m_rasterBucketsHistogramBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_rasterBucketsHistogramBufferPhase2->SetName("Raster bucket histogram phase2");

        m_rasterBucketsWriteCursorBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
        m_rasterBucketsWriteCursorBufferPhase2->SetName("CLod Raster bucket write cursor phase2");

        m_type = type;
    }

    void OnRegistryReset(ResourceRegistry* reg) override {

    }

    void GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override {

        // First hierarchial culling pass
        // Occlusion culls based on last frame's depth buffer
        // Anything that fails only occlusion test gets sent to phase 2 
        // Any clusters that pass gets written out as a visible cluster
        RenderGraph::ExternalPassDesc cullPassDesc;
        cullPassDesc.type = RenderGraph::PassType::Compute;
        cullPassDesc.name = "CLod::HierarchialCullingPass1";
        HierarchialCullingPassInputs cullPassInputs;
        cullPassInputs.isFirstPass = true;
        cullPassDesc.pass = std::make_shared<HierarchialCullingPass>(
            cullPassInputs,
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_workGraphTelemetryBuffer,
            m_occlusionReplayBuffer,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            m_viewDepthSrvIndicesBuffer);
        cullPassDesc.where = RenderGraph::ExternalInsertPoint::After("SkinningPass");
        outPasses.push_back(std::move(cullPassDesc));

		// Histogram + prefix sum passes to prepare for indirect dispatch rasterization
        RenderGraph::ExternalPassDesc histogramPassDesc;
        histogramPassDesc.type = RenderGraph::PassType::Compute;
        histogramPassDesc.name = "CLod::RasterBucketsHistogramPass1";
        histogramPassDesc.pass = std::make_shared<RasterBucketHistogramPass>(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBuffer);
        outPasses.push_back(std::move(histogramPassDesc));

        RenderGraph::ExternalPassDesc prefixScanPassDesc;
        prefixScanPassDesc.type = RenderGraph::PassType::Compute;
        prefixScanPassDesc.name = "CLod::RasterBucketsPrefixScanPass1";
        prefixScanPassDesc.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer);
        outPasses.push_back(std::move(prefixScanPassDesc));

        RenderGraph::ExternalPassDesc prefixOffsetsPassDesc;
        prefixOffsetsPassDesc.type = RenderGraph::PassType::Compute;
        prefixOffsetsPassDesc.name = "CLod::RasterBucketsPrefixOffsetsPass1";
        prefixOffsetsPassDesc.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer);
        outPasses.push_back(std::move(prefixOffsetsPassDesc));

        RenderGraph::ExternalPassDesc compactPassDesc;
        compactPassDesc.type = RenderGraph::PassType::Compute;
        compactPassDesc.name = "CLod::RasterBucketsCompactAndArgsPass1";
        compactPassDesc.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsWriteCursorBuffer,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_maxVisibleClusters,
            false);
        outPasses.push_back(std::move(compactPassDesc));

		// First rasterization pass, using indirect args from histogram compaction
        RenderGraph::ExternalPassDesc rasterizePassDesc;
        rasterizePassDesc.type = RenderGraph::PassType::Render;
        rasterizePassDesc.name = "CLod::RasterizeClustersPass1";
        ClusterRasterizationPassInputs rasterizePassInputs;
        rasterizePassInputs.clearGbuffer = true;
        rasterizePassInputs.wireframe = false;
        rasterizePassDesc.pass = std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsIndirectArgsBuffer);
        rasterizePassDesc.isGeometryPass = true;
        outPasses.push_back(std::move(rasterizePassDesc));

        // If we are writing a visibility buffer, we need to extract depth before downsample
        if (m_type != CLodExtensionType::Shadow) {
            RenderGraph::ExternalPassDesc depthCopyPassDesc;
            depthCopyPassDesc.type = RenderGraph::PassType::Compute;
            depthCopyPassDesc.name = "CLod::LinearDepthCopyPass1";
            depthCopyPassDesc.pass = std::make_shared<PerViewLinearDepthCopyPass>();
            outPasses.push_back(std::move(depthCopyPassDesc));
        }

		// Build HZB for occlusion culling in phase 2
        RenderGraph::ExternalPassDesc downsamplePassDesc;
        downsamplePassDesc.type = RenderGraph::PassType::Compute;
        downsamplePassDesc.name = "CLod::LinearDepthDownsamplePass1";
        downsamplePassDesc.pass = std::make_shared<DownsamplePass>();
        outPasses.push_back(std::move(downsamplePassDesc));

		// Second hierarchial culling pass
		// Anything that failed only occlusion in the first pass gets another chance,
		// using the HZB built from this frame's depth buffer
        RenderGraph::ExternalPassDesc cullPassDesc2;
        cullPassDesc2.type = RenderGraph::PassType::Compute;
        cullPassDesc2.name = "CLod::HierarchialCullingPass2";
        HierarchialCullingPassInputs cullPassInputs2;
        cullPassInputs2.isFirstPass = false;
        cullPassDesc2.pass = std::make_shared<HierarchialCullingPass>(
            cullPassInputs2,
            m_visibleClustersBuffer,
            m_visibleClustersCounterBufferPhase2,
            m_histogramIndirectCommand,
            m_workGraphTelemetryBuffer,
            m_occlusionReplayBuffer,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            m_viewDepthSrvIndicesBuffer);
        outPasses.push_back(std::move(cullPassDesc2));

		// Histogram + prefix sum passes again
        RenderGraph::ExternalPassDesc histogramPassDesc2;
        histogramPassDesc2.type = RenderGraph::PassType::Compute;
        histogramPassDesc2.name = "CLod::RasterBucketsHistogramPass2";
        histogramPassDesc2.pass = std::make_shared<RasterBucketHistogramPass>(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBufferPhase2,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferPhase2);
        outPasses.push_back(std::move(histogramPassDesc2));

        RenderGraph::ExternalPassDesc prefixScanPassDesc2;
        prefixScanPassDesc2.type = RenderGraph::PassType::Compute;
        prefixScanPassDesc2.name = "CLod::RasterBucketsPrefixScanPass2";
        prefixScanPassDesc2.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBufferPhase2,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer);
        outPasses.push_back(std::move(prefixScanPassDesc2));

        RenderGraph::ExternalPassDesc prefixOffsetsPassDesc2;
        prefixOffsetsPassDesc2.type = RenderGraph::PassType::Compute;
        prefixOffsetsPassDesc2.name = "CLod::RasterBucketsPrefixOffsetsPass2";
        prefixOffsetsPassDesc2.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer);
        outPasses.push_back(std::move(prefixOffsetsPassDesc2));

        RenderGraph::ExternalPassDesc compactPassDesc2;
        compactPassDesc2.type = RenderGraph::PassType::Compute;
        compactPassDesc2.name = "CLod::RasterBucketsCompactAndArgsPass2";
        compactPassDesc2.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBufferPhase2,
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferPhase2,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsWriteCursorBufferPhase2,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_maxVisibleClusters,
            true);
        outPasses.push_back(std::move(compactPassDesc2));

		// Second rasterization pass, using indirect args from phase 2 histogram compaction
        RenderGraph::ExternalPassDesc rasterizePassDesc2;
        rasterizePassDesc2.type = RenderGraph::PassType::Render;
        rasterizePassDesc2.name = "CLod::RasterizeClustersPass2";
        rasterizePassDesc2.where = RenderGraph::ExternalInsertPoint::Before("MaterialHistogramPass");
        ClusterRasterizationPassInputs rasterizePassInputs2;
        rasterizePassInputs2.clearGbuffer = false;
        rasterizePassInputs2.wireframe = false;
        rasterizePassDesc2.pass = std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs2,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBufferPhase2,
            m_rasterBucketsIndirectArgsBuffer);
        rasterizePassDesc2.isGeometryPass = true;
        outPasses.push_back(std::move(rasterizePassDesc2));

        // Ensure linear depth contains any updates introduced by the second CLod raster pass
        if (m_type != CLodExtensionType::Shadow) {
            RenderGraph::ExternalPassDesc depthCopyPassDesc2;
            depthCopyPassDesc2.type = RenderGraph::PassType::Compute;
            depthCopyPassDesc2.name = "CLod::LinearDepthCopyPass2";
			depthCopyPassDesc2.where = RenderGraph::ExternalInsertPoint::Before("DeferredShadingPass");
            depthCopyPassDesc2.pass = std::make_shared<PerViewLinearDepthCopyPass>();
            outPasses.push_back(std::move(depthCopyPassDesc2));
        }
    }

private:

    CLodExtensionType m_type;
    uint64_t m_maxVisibleClusters;

    // Buffers used across CLod passes
    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
    std::shared_ptr<Buffer> m_workGraphTelemetryBuffer;
    std::shared_ptr<Buffer> m_occlusionReplayBuffer;
    std::shared_ptr<Buffer> m_occlusionReplayStateBuffer;
    std::shared_ptr<Buffer> m_occlusionNodeGpuInputsBuffer;
    std::shared_ptr<Buffer> m_viewDepthSrvIndicesBuffer;

    // Histogram Pass Buffers
    std::shared_ptr<Buffer> m_histogramIndirectCommand;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBuffer;

    // prefix scan buffers
    std::shared_ptr<Buffer> m_rasterBucketsOffsetsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsBlockSumsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsScannedBlockSumsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsTotalCountBuffer;

    // Phase-2 CPU-reset resources (duplicated to avoid pre-execute Update clobbering)
    std::shared_ptr<Buffer> m_visibleClustersCounterBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBufferPhase2;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBufferPhase2;

    // Compaction Pass Buffers
    std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsWriteCursorBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBuffer;

    class PerViewLinearDepthCopyPass : public ComputePass {
    public:
        PerViewLinearDepthCopyPass() {
            m_pso = PSOManager::GetInstance().MakeComputePipeline(
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                L"shaders/gbuffer.hlsl",
                L"PerViewPrimaryDepthCopyCS",
                {},
                "PerViewPrimaryDepthCopyPSO");
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(Builtin::PrimaryCamera::VisibilityTexture)
                .WithUnorderedAccess(Builtin::PrimaryCamera::LinearDepthMap, Builtin::Shadows::LinearShadowMaps);
        }

        void Setup() override {}

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
            auto& context = *renderContext;
            auto& commandList = executionContext.commandList;

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
            commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());

            uint32_t rootConstants[NumMiscUintRootConstants] = {};

            context.viewManager->ForEachView([&](uint64_t viewID) {
                const auto* view = context.viewManager->Get(viewID);
                if (!view || !view->gpu.visibilityBuffer || !view->gpu.linearDepthMap) {
                    return;
                }

                rootConstants[UintRootConstant0] = view->gpu.visibilityBuffer->GetSRVInfo(0).slot.index;
                rootConstants[UintRootConstant1] = view->gpu.linearDepthMap->GetUAVShaderVisibleInfo(0).slot.index;
                rootConstants[UintRootConstant2] = view->gpu.visibilityBuffer->GetWidth();
                rootConstants[UintRootConstant3] = view->gpu.visibilityBuffer->GetHeight();

                commandList.PushConstants(
                    rhi::ShaderStage::Compute,
                    0,
                    MiscUintRootSignatureIndex,
                    0,
                    NumMiscUintRootConstants,
                    rootConstants);

                const uint32_t groupsX = (rootConstants[UintRootConstant2] + 7u) / 8u;
                const uint32_t groupsY = (rootConstants[UintRootConstant3] + 7u) / 8u;
                commandList.Dispatch(groupsX, groupsY, 1);
            });

            return {};
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
    };

    // ---------------------------------------------------------------
    // Hierarchial Culling Pass
    // ---------------------------------------------------------------

    struct ObjectCullRecord
    {
        uint viewDataIndex; // One record per view *...
        uint activeDrawSetIndicesSRVIndex; // One record per draw set
        uint activeDrawCount;
        uint dispatchGridX; // Drives dispatch size
        uint dispatchGridY;
        uint dispatchGridZ;
    };

    struct HierarchialCullingPassInputs {
        bool isFirstPass;

        friend bool operator==(const HierarchialCullingPassInputs&, const HierarchialCullingPassInputs&) = default;
    };

    inline rg::Hash64 HashValue(const HierarchialCullingPassInputs& i) {
        std::size_t seed = 0;

        boost::hash_combine(seed, i.isFirstPass);
        return seed;
    }

    class HierarchialCullingPass : public ComputePass, public IDynamicDeclaredResources {
    public:
        HierarchialCullingPass(
            HierarchialCullingPassInputs inputs,
            std::shared_ptr<Buffer> visibleClustersBuffer,
            std::shared_ptr<Buffer> visibleClustersCounterBuffer,
            std::shared_ptr<Buffer> histogramIndirectCommand,
            std::shared_ptr<Buffer> workGraphTelemetryBuffer,
            std::shared_ptr<Buffer> occlusionReplayBuffer,
            std::shared_ptr<Buffer> occlusionReplayStateBuffer,
            std::shared_ptr<Buffer> occlusionNodeGpuInputsBuffer,
            std::shared_ptr<Buffer> viewDepthSrvIndicesBuffer) {
            CreatePipelines(
                DeviceManager::GetInstance().GetDevice(),
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_workGraph,
                m_createCommandPipelineState);
            m_isFirstPass = inputs.isFirstPass;
            auto memSize = m_workGraph->GetRequiredScratchMemorySize();
            m_scratchBuffer = Buffer::CreateShared( // TODO: Make a way for the graph to provide things like this, to allow for aliasing
                rhi::HeapType::DeviceLocal,
                memSize,
                true);
            m_scratchBuffer->SetMemoryUsageHint("Work graph scratch buffer");
            m_visibleClustersBuffer = visibleClustersBuffer;
            m_visibleClustersCounterBuffer = visibleClustersCounterBuffer;
            m_histogramIndirectCommand = histogramIndirectCommand;
            m_workGraphTelemetryBuffer = workGraphTelemetryBuffer;
            m_occlusionReplayBuffer = occlusionReplayBuffer;
            m_occlusionReplayStateBuffer = occlusionReplayStateBuffer;
            m_occlusionNodeGpuInputsBuffer = occlusionNodeGpuInputsBuffer;
            m_viewDepthSrvIndicesBuffer = viewDepthSrvIndicesBuffer;
        }

        ~HierarchialCullingPass() {
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            auto ecsWorld = ECSManager::GetInstance().GetWorld();
            flecs::query<> drawSetIndicesQuery = ecsWorld.query_builder<>()
                .with<Components::IsActiveDrawSetIndices>()
                .with<Components::ParticipatesInPass>(flecs::Wildcard)
                .build();
            builder->WithUnorderedAccess(m_scratchBuffer,
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_histogramIndirectCommand,
                m_workGraphTelemetryBuffer,
                m_occlusionReplayBuffer,
                m_occlusionReplayStateBuffer,
                m_occlusionNodeGpuInputsBuffer,
                m_viewDepthSrvIndicesBuffer)
                .WithShaderResource(Builtin::IndirectCommandBuffers::Master,
                    Builtin::CLod::Offsets,
                    Builtin::CLod::Groups,
                    Builtin::CLod::Children,
                    Builtin::CLod::ChildLocalMeshletIndices,
                    Builtin::CLod::Nodes,
                    Builtin::CLod::MeshletBounds,
                    Builtin::CullingCameraBuffer,
                    Builtin::PerMeshInstanceBuffer,
                    Builtin::PerObjectBuffer,
                    Builtin::CameraBuffer,
                    Builtin::PerMeshBuffer,
                    Builtin::PrimaryCamera::LinearDepthMap, 
                    Builtin::Shadows::LinearShadowMaps)
                .WithShaderResource(ECSResourceResolver(drawSetIndicesQuery));

        }

        void Setup() override {
            RegisterSRV(Builtin::IndirectCommandBuffers::Master);
            RegisterSRV(Builtin::CLod::Offsets);
            RegisterSRV(Builtin::CLod::Groups);
            RegisterSRV(Builtin::CLod::Children);
            RegisterSRV(Builtin::CLod::ChildLocalMeshletIndices);
            RegisterSRV(Builtin::CullingCameraBuffer);
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerObjectBuffer);
            RegisterSRV(Builtin::CLod::Nodes);
            RegisterSRV(Builtin::CLod::MeshletBounds);
            RegisterSRV(Builtin::CameraBuffer);
            RegisterSRV(Builtin::PerMeshBuffer);

            //RegisterUAV(Builtin::VisibleClusterBuffer);
            //RegisterUAV(Builtin::VisibleClusterCounter);

            //m_visibleClusterCounter = m_resourceRegistryView->RequestHandle(Builtin::VisibleClusterCounter);
        }

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
            auto& context = *renderContext;
            auto& commandList = executionContext.commandList;

            // Set the descriptor heaps
            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

            commandList.SetWorkGraph(m_workGraph->GetHandle(), m_scratchBuffer->GetAPIResource().GetHandle(), true); // Reset every time for now

            BindResourceDescriptorIndices(commandList, m_pipelineResources);

            uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_RASTER_BUCKET_HISTOGRAM_COMMAND_DESCRIPTOR_INDEX] = m_histogramIndirectCommand->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_WORKGRAPH_TELEMETRY_DESCRIPTOR_INDEX] = m_workGraphTelemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_WORKGRAPH_TELEMETRY_ENABLED] = IsCLodWorkGraphTelemetryEnabled() ? 1u : 0u;
            uintRootConstants[CLOD_OCCLUSION_REPLAY_BUFFER_DESCRIPTOR_INDEX] = m_occlusionReplayBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX] = m_occlusionReplayStateBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_WORKGRAPH_NODE_INPUTS_DESCRIPTOR_INDEX] = m_occlusionNodeGpuInputsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX] = m_viewDepthSrvIndicesBuffer->GetSRVInfo(0).slot.index;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                uintRootConstants);

            if (m_isFirstPass) {
                std::vector<ObjectCullRecord> cullRecords;

                ViewFilter filter = ViewFilter::PrimaryCameras();
                context.viewManager->ForEachFiltered(filter, [&](uint64_t view) {
                    auto viewInfo = context.viewManager->Get(view);
                    auto cameraBufferIndex = viewInfo->gpu.cameraBufferIndex;
                    auto workloads = context.indirectCommandBufferManager->GetViewIndirectBuffersForRenderPhase(view, m_renderPhase);
                    for (auto& wl : workloads) {
                        auto count = wl.workload.count;
                        if (count == 0) {
                            continue;
                        }
                        ObjectCullRecord record{};
                        record.viewDataIndex = cameraBufferIndex;
                        record.activeDrawSetIndicesSRVIndex = context.objectManager->GetActiveDrawSetIndices(wl.flags)->GetSRVInfo(0).slot.index;
                        record.activeDrawCount = count;
                        record.dispatchGridX = static_cast<uint>((count + 63) / 64);
                        record.dispatchGridY = 1;
                        record.dispatchGridZ = 1;
                        cullRecords.push_back(record);
                    }
                    });

                rhi::WorkGraphDispatchDesc dispatchDesc{};
                dispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::NodeCpuInput;
                dispatchDesc.nodeCpuInput.entryPointIndex = 0;
                dispatchDesc.nodeCpuInput.pRecords = cullRecords.data();
                dispatchDesc.nodeCpuInput.numRecords = static_cast<uint32_t>(cullRecords.size());
                dispatchDesc.nodeCpuInput.recordByteStride = sizeof(ObjectCullRecord);
                commandList.DispatchWorkGraph(dispatchDesc);

                rhi::BufferBarrier barrier{};
                barrier.buffer = m_visibleClustersCounterBuffer->GetAPIResource().GetHandle();
                barrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
                barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
                barrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
                barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
                rhi::BarrierBatch bufferBarriers{};
                bufferBarriers.buffers = rhi::Span<rhi::BufferBarrier>(&barrier, 1);
                commandList.Barriers(bufferBarriers);

            }
            else {
                rhi::BufferBarrier replayDispatchBarriers[2] = {};
                replayDispatchBarriers[0].buffer = m_occlusionReplayBuffer->GetAPIResource().GetHandle();
                replayDispatchBarriers[0].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
                replayDispatchBarriers[0].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
                replayDispatchBarriers[0].beforeSync = rhi::ResourceSyncState::ComputeShading;
                replayDispatchBarriers[0].afterSync = rhi::ResourceSyncState::ComputeShading;

                replayDispatchBarriers[1].buffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
                replayDispatchBarriers[1].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
                replayDispatchBarriers[1].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
                replayDispatchBarriers[1].beforeSync = rhi::ResourceSyncState::ComputeShading;
                replayDispatchBarriers[1].afterSync = rhi::ResourceSyncState::ComputeShading;

                rhi::BarrierBatch replayBarrierBatch{};
                replayBarrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(replayDispatchBarriers, 2);
                commandList.Barriers(replayBarrierBatch);

                rhi::WorkGraphDispatchDesc replayDispatchDesc{};
                replayDispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::MultiNodeGpuInput;
                replayDispatchDesc.multiNodeGpuInput.inputBuffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
                replayDispatchDesc.multiNodeGpuInput.inputAddressOffset = 0;
                commandList.DispatchWorkGraph(replayDispatchDesc);
            }

            rhi::BufferBarrier counterBarrier{};
            counterBarrier.buffer = m_visibleClustersCounterBuffer->GetAPIResource().GetHandle();
            counterBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
            counterBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            counterBarrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
            counterBarrier.afterSync = rhi::ResourceSyncState::ComputeShading;
            rhi::BarrierBatch counterBarrierBatch{};
            counterBarrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(&counterBarrier, 1);
            commandList.Barriers(counterBarrierBatch);

            BindResourceDescriptorIndices(commandList, m_createCommandPipelineState.GetResourceDescriptorSlots());
            uintRootConstants[CLOD_NUM_RASTER_BUCKETS] = context.materialManager->GetRasterBucketCount();
            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                uintRootConstants);

            commandList.BindPipeline(m_createCommandPipelineState.GetAPIPipelineState().GetHandle());
            commandList.Dispatch(1, 1, 1);

            // TODO: Move to post-raster pass
            //rhi::BufferBarrier replayDispatchBarriers[2] = {};
            //replayDispatchBarriers[0].buffer = m_occlusionReplayBuffer->GetAPIResource().GetHandle();
            //replayDispatchBarriers[0].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
            //replayDispatchBarriers[0].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            //replayDispatchBarriers[0].beforeSync = rhi::ResourceSyncState::ComputeShading;
            //replayDispatchBarriers[0].afterSync = rhi::ResourceSyncState::ComputeShading;

            //replayDispatchBarriers[1].buffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
            //replayDispatchBarriers[1].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
            //replayDispatchBarriers[1].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            //replayDispatchBarriers[1].beforeSync = rhi::ResourceSyncState::ComputeShading;
            //replayDispatchBarriers[1].afterSync = rhi::ResourceSyncState::ComputeShading;

            //rhi::BarrierBatch replayBarrierBatch{};
            //replayBarrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(replayDispatchBarriers, 2);
            //commandList.Barriers(replayBarrierBatch);

            //rhi::WorkGraphDispatchDesc replayDispatchDesc{};
            //replayDispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::MultiNodeGpuInput;
            //replayDispatchDesc.multiNodeGpuInput.inputBuffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
            //replayDispatchDesc.multiNodeGpuInput.inputAddressOffset = 0;
            //commandList.DispatchWorkGraph(replayDispatchDesc);

            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
			auto* updateContext = executionContext.hostData ? executionContext.hostData->Get<UpdateContext>() : nullptr;
			if (!updateContext) return;
			auto& context = *updateContext;
            m_declaredResourcesChanged = false;

            uint32_t zero = 0u;
            BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_visibleClustersCounterBuffer), 0);

            if (!m_isFirstPass) {
                return;
            }

            CLodReplayBufferState replayState{};
            replayState.nodeGroupWriteOffsetBytes = 0;
            replayState.meshletWriteOffsetBytes = CLodReplayBufferSizeBytes;
            replayState.nodeGroupDroppedRecords = 0;
            replayState.meshletDroppedRecords = 0;
            BUFFER_UPLOAD(
                &replayState,
                sizeof(CLodReplayBufferState),
                rg::runtime::UploadTarget::FromShared(m_occlusionReplayStateBuffer),
                0);

            std::vector<CLodViewDepthSRVIndex> viewDepthSrvIndices(CLodMaxViewDepthIndices);
            for (uint32_t i = 0; i < CLodMaxViewDepthIndices; ++i) {
                viewDepthSrvIndices[i].cameraBufferIndex = i;
                viewDepthSrvIndices[i].linearDepthSRVIndex = 0;
            }

            context.viewManager->ForEachView([&](uint64_t viewID) {
                const auto* view = context.viewManager->Get(viewID);
                if (!view || !view->gpu.linearDepthMap) {
                    return;
                }

                const uint32_t cameraBufferIndex = view->gpu.cameraBufferIndex;
                if (cameraBufferIndex >= CLodMaxViewDepthIndices) {
                    return;
                }

                const auto linearDepthMap = view->gpu.linearDepthMap;
                uint32_t slice = 0;
                if (view->cameraInfo.depthBufferArrayIndex >= 0) {
                    slice = static_cast<uint32_t>(view->cameraInfo.depthBufferArrayIndex);
                }

                const uint32_t maxSlices = linearDepthMap->GetNumSRVSlices();
                if (maxSlices == 0) {
                    return;
                }

                slice = (std::min)(slice, maxSlices - 1);
                viewDepthSrvIndices[cameraBufferIndex].cameraBufferIndex = cameraBufferIndex;
                viewDepthSrvIndices[cameraBufferIndex].linearDepthSRVIndex = linearDepthMap->GetSRVInfo(0, slice).slot.index;
            });

            BUFFER_UPLOAD(
                viewDepthSrvIndices.data(),
                static_cast<uint32_t>(viewDepthSrvIndices.size() * sizeof(CLodViewDepthSRVIndex)),
                rg::runtime::UploadTarget::FromShared(m_viewDepthSrvIndicesBuffer),
                0);

            CLodNodeGpuInput nodeGpuInputs[3] = {};
            CLodMultiNodeGpuInput multiNodeGpuInput{};
            multiNodeGpuInput.numNodeInputs = 2;
            multiNodeGpuInput.pad0 = 0;
            multiNodeGpuInput.nodeInputStride = sizeof(CLodNodeGpuInput);

            if (ID3D12Resource* nodeInputResource = rhi::dx12::get_resource(m_occlusionNodeGpuInputsBuffer->GetAPIResource())) {
                const uint64_t nodeInputBufferAddress = nodeInputResource->GetGPUVirtualAddress();
                multiNodeGpuInput.nodeInputsAddress = nodeInputBufferAddress + sizeof(CLodNodeGpuInput);
            }

            if (ID3D12Resource* replayResource = rhi::dx12::get_resource(m_occlusionReplayBuffer->GetAPIResource())) {
                const uint64_t replayAddress = replayResource->GetGPUVirtualAddress();
                nodeGpuInputs[1].entrypointIndex = 1;
                nodeGpuInputs[1].numRecords = 0;
                nodeGpuInputs[1].recordsAddress = replayAddress;
                nodeGpuInputs[1].recordStride = sizeof(CLodNodeGroupReplayRecord);

                nodeGpuInputs[2].entrypointIndex = 2;
                nodeGpuInputs[2].numRecords = 0;
                nodeGpuInputs[2].recordsAddress = replayAddress + CLodReplayBufferSizeBytes;
                nodeGpuInputs[2].recordStride = sizeof(CLodMeshletReplayRecord);
            }

            static_assert(sizeof(CLodMultiNodeGpuInput) == sizeof(CLodNodeGpuInput));
            std::memcpy(&nodeGpuInputs[0], &multiNodeGpuInput, sizeof(CLodMultiNodeGpuInput));

            BUFFER_UPLOAD(
                nodeGpuInputs,
                sizeof(nodeGpuInputs),
                rg::runtime::UploadTarget::FromShared(m_occlusionNodeGpuInputsBuffer),
                0);

            if (IsCLodWorkGraphTelemetryEnabled()) {
                std::vector<uint32_t> zeroTelemetry(CLodWorkGraphCounterCount, 0u);
                BUFFER_UPLOAD(
                    zeroTelemetry.data(),
                    static_cast<uint32_t>(zeroTelemetry.size() * sizeof(uint32_t)),
                    rg::runtime::UploadTarget::FromShared(m_workGraphTelemetryBuffer),
                    0);
            }
        }

        bool DeclaredResourcesChanged() const override {
            return m_declaredResourcesChanged;
        }

        void Cleanup() override {

        }

    private:
        PipelineResources m_pipelineResources;
        rhi::WorkGraphPtr m_workGraph;
        PipelineState m_createCommandPipelineState;
        std::shared_ptr<Buffer> m_visibleClustersBuffer;
        std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
        std::shared_ptr<Buffer> m_scratchBuffer;
        std::shared_ptr<Buffer> m_histogramIndirectCommand;
        std::shared_ptr<Buffer> m_workGraphTelemetryBuffer;
        std::shared_ptr<Buffer> m_occlusionReplayBuffer;
        std::shared_ptr<Buffer> m_occlusionReplayStateBuffer;
        std::shared_ptr<Buffer> m_occlusionNodeGpuInputsBuffer;
        std::shared_ptr<Buffer> m_viewDepthSrvIndicesBuffer;
        bool m_isFirstPass = true;
        bool m_declaredResourcesChanged = true;
        RenderPhase m_renderPhase = Engine::Primary::GBufferPass;

        void CreatePipelines(
            rhi::Device device,
            rhi::PipelineLayoutHandle globalRootSignature,
            rhi::WorkGraphPtr& outGraph,
            PipelineState& outCreateCommandPipeline)
        {
            // Compile the work-graph library
            ShaderLibraryInfo libInfo(L"shaders/ClusterLOD/workGraphCulling.hlsl", L"lib_6_8");
            auto compiled = PSOManager::GetInstance().CompileShaderLibrary(libInfo);
            m_pipelineResources = compiled.resourceDescriptorSlots;

            rhi::ShaderBinary libDxil{
                compiled.libraryBlob->GetBufferPointer(),
                static_cast<uint32_t>(compiled.libraryBlob->GetBufferSize())
            };

            // Export the node shader symbols from the library
            // These are the *export names* (function symbols), not NodeID strings.
            std::array<rhi::ShaderExportDesc, 6> exports = { {
                { "WG_ObjectCull",   nullptr },
                { "WG_ReplayNodeGroup", nullptr },
                { "WG_ReplayMeshlet", nullptr },
                { "WG_TraverseNodes",     nullptr },
                { "WG_GroupEvaluate",     nullptr },
                { "WG_ClusterCullBuckets",  nullptr },
            } };

            rhi::ShaderLibraryDesc library{};
            library.dxil = libDxil;
            library.exports = rhi::Span<rhi::ShaderExportDesc>(exports.data(), static_cast<uint32_t>(exports.size()));

            std::array<rhi::ShaderLibraryDesc, 1> libraries = { library };

            // Entry point is by NodeID (the [NodeID("ObjectCull")] in HLSL)
            std::array<rhi::NodeIDDesc, 3> entrypoints = { {
                { "ObjectCull", 0 },
                { "ReplayNodeGroup", 0 },
                { "ReplayMeshlet", 0 }
            } };

            // Build the work graph desc
            rhi::WorkGraphDesc wg{};
            wg.programName = "HierarchialCulling";
            wg.flags = rhi::WorkGraphFlags::WorkGraphFlagsIncludeAllAvailableNodes;
            wg.globalRootSignature = globalRootSignature;
            wg.libraries = rhi::Span<rhi::ShaderLibraryDesc>(libraries.data(), static_cast<uint32_t>(libraries.size()));
            wg.entrypoints = rhi::Span<rhi::NodeIDDesc>(entrypoints.data(), static_cast<uint32_t>(entrypoints.size()));
            wg.allowStateObjectAdditions = false;
            wg.debugName = "HierarchialCullingWG";

            // Create
            device.CreateWorkGraph(wg, outGraph);

            // Pipeline to create indirect command
            outCreateCommandPipeline = PSOManager::GetInstance().MakeComputePipeline(
                globalRootSignature,
                L"shaders/ClusterLOD/clodUtil.hlsl",
                L"CreateRasterBucketsHistogramCommandCSMain",
                {},
                "HierarchialLODCommandCreation");
        }
    };

    // --------------------------------------------------------------
    // Raster Bucket Histogram Pass
    // --------------------------------------------------------------

    class RasterBucketHistogramPass : public ComputePass {
    public:
        RasterBucketHistogramPass(
            std::shared_ptr<Buffer> visibleClustersBuffer,
            std::shared_ptr<Buffer> visibleClustersCounterBuffer,
            std::shared_ptr<Buffer> histogramIndirectCommand,
            std::shared_ptr<Buffer> histogramBuffer) {
            CreatePipelines(
                DeviceManager::GetInstance().GetDevice(),
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_histogramPipeline);

            // Used by the cluster rasterization pass
            rhi::IndirectArg rasterizeClustersArgs[] = {
                {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
                {.kind = rhi::IndirectArgKind::Dispatch }
            };

            auto device = DeviceManager::GetInstance().GetDevice();

            auto result = device.CreateCommandSignature(
                rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(rasterizeClustersArgs, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(), m_histogramCommandSignature);

            m_visibleClustersBuffer = visibleClustersBuffer;
            m_visibleClustersCounterBuffer = visibleClustersCounterBuffer;
            m_histogramIndirectCommand = histogramIndirectCommand;
            m_histogramBuffer = histogramBuffer;
        }

        ~RasterBucketHistogramPass() {
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                Builtin::PerMeshBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerMaterialDataBuffer)
                .WithIndirectArguments(m_histogramIndirectCommand)
                .WithUnorderedAccess(m_histogramBuffer);
        }

        void Setup() override {
            RegisterSRV(Builtin::PerMeshBuffer);
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerMaterialDataBuffer);
        }

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
            auto& context = *renderContext;
            auto& commandList = executionContext.commandList;

            // Set the descriptor heaps
            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

            commandList.BindPipeline(m_histogramPipeline.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_histogramPipeline.GetResourceDescriptorSlots());

            uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
            uintRootConstants[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                uintRootConstants);

            // Single-dispatch ExecuteIndirect
            commandList.ExecuteIndirect(m_histogramCommandSignature->GetHandle(), m_histogramIndirectCommand->GetAPIResource().GetHandle(), 0, {}, 0, 1);

            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
			auto& context = *updateContext;

            auto numRasterBuckets = context.materialManager->GetRasterBucketCount();

            // Resize histogram buffer if needed
            if (m_histogramBuffer->GetSize() < static_cast<size_t>(numRasterBuckets) * sizeof(uint32_t)) {
                m_histogramBuffer->ResizeStructured(numRasterBuckets);
            }

            // Clear the histogram buffer
            std::vector<uint32_t> zeroData(numRasterBuckets, 0);
            BUFFER_UPLOAD(zeroData.data(), static_cast<uint32_t>(zeroData.size() * sizeof(uint32_t)), rg::runtime::UploadTarget::FromShared(m_histogramBuffer), 0);
        }

        void Cleanup() override {

        }

    private:
        PipelineState m_histogramPipeline;
        rhi::CommandSignaturePtr m_histogramCommandSignature;
        std::shared_ptr<Buffer> m_visibleClustersBuffer;
        std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
        std::shared_ptr<Buffer> m_histogramIndirectCommand;
        std::shared_ptr<Buffer> m_histogramBuffer;

        void CreatePipelines(
            rhi::Device device,
            rhi::PipelineLayoutHandle globalRootSignature,
            PipelineState& outHistogramPipeline)
        {
            outHistogramPipeline = PSOManager::GetInstance().MakeComputePipeline(
                globalRootSignature,
                L"Shaders/ClusterLOD/clodUtil.hlsl",
                L"ClusterRasterBucketsHistogramCSMain");
        }
    };

    // --------------------------------------------------------------
    // Raster Bucket Prefix Sum Passes
    // --------------------------------------------------------------

    class RasterBucketBlockScanPass : public ComputePass {
    public:
        RasterBucketBlockScanPass(
            std::shared_ptr<Buffer> histogramBuffer,
            std::shared_ptr<Buffer> offsetsBuffer,
            std::shared_ptr<Buffer> blockSumsBuffer)
            : m_histogramBuffer(std::move(histogramBuffer)),
            m_offsetsBuffer(std::move(offsetsBuffer)),
            m_blockSumsBuffer(std::move(blockSumsBuffer)) {
            m_pso = PSOManager::GetInstance().MakeComputePipeline(
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                L"Shaders/ClusterLOD/clodUtil.hlsl",
                L"RasterBucketsBlockScanCS",
                {},
                "CLod_RasterBucketsBlockScanPSO");
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(m_histogramBuffer)
                .WithUnorderedAccess(m_offsetsBuffer, m_blockSumsBuffer);
        }

        void Setup() override {}

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
			auto& context = *renderContext;
            auto& commandList = executionContext.commandList;
            auto& pm = PSOManager::GetInstance();

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());
            commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

            uint32_t rc[NumMiscUintRootConstants] = {};
            rc[UintRootConstant0] = numBuckets;
            rc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_blockSumsBuffer->GetUAVShaderVisibleInfo(0).slot.index;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                rc);

            commandList.Dispatch(numBlocks, 1, 1);
            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
			auto& context = *updateContext;
            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            if (m_offsetsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
                m_offsetsBuffer->ResizeStructured(numBuckets);
            }
            if (m_blockSumsBuffer->GetSize() < static_cast<size_t>(numBlocks) * sizeof(uint32_t)) {
                m_blockSumsBuffer->ResizeStructured(numBlocks);
            }
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
        uint32_t m_blockSize = 1024;
        std::shared_ptr<Buffer> m_histogramBuffer;
        std::shared_ptr<Buffer> m_offsetsBuffer;
        std::shared_ptr<Buffer> m_blockSumsBuffer;
    };

    class RasterBucketBlockOffsetsPass : public ComputePass {
    public:
        RasterBucketBlockOffsetsPass(
            std::shared_ptr<Buffer> offsetsBuffer,
            std::shared_ptr<Buffer> blockSumsBuffer,
            std::shared_ptr<Buffer> scannedBlockSumsBuffer,
            std::shared_ptr<Buffer> totalCountBuffer)
            : m_offsetsBuffer(std::move(offsetsBuffer)),
            m_blockSumsBuffer(std::move(blockSumsBuffer)),
            m_scannedBlockSumsBuffer(std::move(scannedBlockSumsBuffer)),
            m_totalCountBuffer(std::move(totalCountBuffer)) {
            m_pso = PSOManager::GetInstance().MakeComputePipeline(
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                L"Shaders/ClusterLOD/clodUtil.hlsl",
                L"RasterBucketsBlockOffsetsCS",
                {},
                "CLod_RasterBucketsBlockOffsetsPSO");
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(m_blockSumsBuffer)
                .WithUnorderedAccess(m_offsetsBuffer, m_scannedBlockSumsBuffer, m_totalCountBuffer);
        }

        void Setup() override {}

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
			auto& context = *renderContext;

            auto& commandList = executionContext.commandList;
            auto& pm = PSOManager::GetInstance();

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());
            commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

            uint32_t rc[NumMiscUintRootConstants] = {};
            rc[UintRootConstant0] = numBuckets;
            rc[UintRootConstant1] = numBlocks;
            rc[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_blockSumsBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_SCANNED_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_scannedBlockSumsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_TOTAL_COUNT_DESCRIPTOR_INDEX] = m_totalCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                rc);

            commandList.Dispatch(1, 1, 1);
            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
			auto& context = *updateContext;
            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            if (m_scannedBlockSumsBuffer->GetSize() < static_cast<size_t>(numBlocks) * sizeof(uint32_t)) {
                m_scannedBlockSumsBuffer->ResizeStructured(numBlocks);
            }
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
        uint32_t m_blockSize = 1024;
        std::shared_ptr<Buffer> m_offsetsBuffer;
        std::shared_ptr<Buffer> m_blockSumsBuffer;
        std::shared_ptr<Buffer> m_scannedBlockSumsBuffer;
        std::shared_ptr<Buffer> m_totalCountBuffer;
    };

    class RasterBucketCompactAndArgsPass : public ComputePass {
    public:
        RasterBucketCompactAndArgsPass(
            std::shared_ptr<Buffer> visibleClustersBuffer,
            std::shared_ptr<Buffer> visibleClustersCounterBuffer,
            std::shared_ptr<Buffer> compactedBaseCounterBuffer,
            std::shared_ptr<Buffer> indirectCommand,
            std::shared_ptr<Buffer> histogramBuffer,
            std::shared_ptr<Buffer> offsetsBuffer,
            std::shared_ptr<Buffer> writeCursorBuffer,
            std::shared_ptr<Buffer> compactedClustersBuffer,
            std::shared_ptr<Buffer> indirectArgsBuffer,
            uint64_t maxVisibleClusters,
            bool appendToExisting)
            : m_visibleClustersBuffer(std::move(visibleClustersBuffer)),
            m_visibleClustersCounterBuffer(std::move(visibleClustersCounterBuffer)),
            m_compactedBaseCounterBuffer(std::move(compactedBaseCounterBuffer)),
            m_indirectCommand(std::move(indirectCommand)),
            m_histogramBuffer(std::move(histogramBuffer)),
            m_offsetsBuffer(std::move(offsetsBuffer)),
            m_writeCursorBuffer(std::move(writeCursorBuffer)),
            m_compactedClustersBuffer(std::move(compactedClustersBuffer)),
            m_indirectArgsBuffer(std::move(indirectArgsBuffer)),
            m_maxVisibleClusters(maxVisibleClusters),
            m_appendToExisting(appendToExisting)
        {
            m_pso = PSOManager::GetInstance().MakeComputePipeline(
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                L"shaders/ClusterLOD/clodUtil.hlsl",
                L"CompactClustersAndBuildIndirectArgsCS",
                {},
                "CLod_RasterBucketsCompactAndArgsPSO");

            rhi::IndirectArg args[] = {
                {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
                {.kind = rhi::IndirectArgKind::Dispatch }
            };

            auto device = DeviceManager::GetInstance().GetDevice();
            device.CreateCommandSignature(
                rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_compactionCommandSignature);
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) override {
            builder->WithShaderResource(
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_compactedBaseCounterBuffer,
                m_histogramBuffer,
                m_offsetsBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerMeshBuffer,
                Builtin::PerMaterialDataBuffer)
                .WithUnorderedAccess(
                    m_writeCursorBuffer,
                    m_compactedClustersBuffer,
                    m_indirectArgsBuffer)
                .WithIndirectArguments(m_indirectCommand);
        }

        void Setup() override {
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerMeshBuffer);
            RegisterSRV(Builtin::PerMaterialDataBuffer);
        }

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
			auto& context = *renderContext;
            auto& commandList = executionContext.commandList;
            auto& pm = PSOManager::GetInstance();

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t kThreads = 64;
            const uint64_t maxItems = std::max<uint64_t>(m_maxVisibleClusters, numBuckets);
            const uint32_t groups = static_cast<uint32_t>((maxItems + kThreads - 1u) / kThreads);

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());
            commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

            unsigned int rc[NumMiscUintRootConstants] = {};
            rc[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_WRITE_CURSOR_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_COMPACTED_APPEND_BASE_COUNTER_DESCRIPTOR_INDEX] = m_compactedBaseCounterBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_NUM_RASTER_BUCKETS] = numBuckets | (m_appendToExisting ? 0x80000000u : 0u);
            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                rc);

            commandList.ExecuteIndirect(
                m_compactionCommandSignature->GetHandle(),
                m_indirectCommand->GetAPIResource().GetHandle(),
                0,
                {},
                0,
                1);

            return {};
        }

        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
			auto& context = *updateContext;
            auto numBuckets = context.materialManager->GetRasterBucketCount();

            if (m_writeCursorBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
                m_writeCursorBuffer->ResizeStructured(numBuckets);
            }
            if (m_indirectArgsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(RasterizeClustersCommand)) {
                m_indirectArgsBuffer->ResizeStructured(numBuckets);
            }

            std::vector<uint32_t> zeroData(numBuckets, 0u);
            BUFFER_UPLOAD(zeroData.data(),
                static_cast<uint32_t>(zeroData.size() * sizeof(uint32_t)),
                rg::runtime::UploadTarget::FromShared(m_writeCursorBuffer),
                0);
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
        rhi::CommandSignaturePtr m_compactionCommandSignature;

        std::shared_ptr<Buffer> m_visibleClustersBuffer;
        std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
        std::shared_ptr<Buffer> m_compactedBaseCounterBuffer;
        std::shared_ptr<Buffer> m_indirectCommand;
        std::shared_ptr<Buffer> m_histogramBuffer;
        std::shared_ptr<Buffer> m_offsetsBuffer;
        std::shared_ptr<Buffer> m_writeCursorBuffer;
        std::shared_ptr<Buffer> m_compactedClustersBuffer;
        std::shared_ptr<Buffer> m_indirectArgsBuffer;

        uint64_t m_maxVisibleClusters = 0;
        bool m_appendToExisting = false;
    };

    // --------------------------------------------------------------
    // Cluster Rasterization Pass
    // --------------------------------------------------------------

    struct ClusterRasterizationPassInputs {
        bool wireframe;
        bool clearGbuffer;

        friend bool operator==(const ClusterRasterizationPassInputs&, const ClusterRasterizationPassInputs&) = default;
    };

    inline rg::Hash64 HashValue(const ClusterRasterizationPassInputs& i) {
        std::size_t seed = 0;

        boost::hash_combine(seed, i.wireframe);
        boost::hash_combine(seed, i.clearGbuffer);
        return seed;
    }

    class ClusterRasterizationPass : public RenderPass, public IDynamicDeclaredResources {
    public:
        ClusterRasterizationPass(
            ClusterRasterizationPassInputs inputs,
            std::shared_ptr<Buffer> compactedVisibleClustersBuffer,
            std::shared_ptr<Buffer> rasterBucketsHistogramBuffer,
            std::shared_ptr<Buffer> rasterBucketsIndirectArgsBuffer)
            : m_compactedVisibleClustersBuffer(std::move(compactedVisibleClustersBuffer)),
            m_rasterBucketsHistogramBuffer(std::move(rasterBucketsHistogramBuffer)),
            m_rasterBucketsIndirectArgsBuffer(std::move(rasterBucketsIndirectArgsBuffer)) {
            m_wireframe = inputs.wireframe;
            m_clearGbuffer = inputs.clearGbuffer;

            m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
            m_viewRasterInfoBuffer->SetName("CLodViewRasterInfoBuffer");

            // Create rasterization indirect command signature
            rhi::IndirectArg args[] = {
                {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 3 } } },
                {.kind = rhi::IndirectArgKind::DispatchMesh }
            };
            auto device = DeviceManager::GetInstance().GetDevice();
            device.CreateCommandSignature(
                rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterizeClustersCommand) },
                PSOManager::GetInstance().GetRootSignature().GetHandle(),
                m_rasterizationCommandSignature);
        }

        ~ClusterRasterizationPass() {
        }

        void DeclareResourceUsages(RenderPassBuilder* builder) {

            builder->WithShaderResource(
                //Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer,
                Builtin::PerObjectBuffer,
                Builtin::NormalMatrixBuffer,
                Builtin::PerMeshBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerMaterialDataBuffer,
                Builtin::PostSkinningVertices,
                Builtin::CameraBuffer,
                Builtin::MeshResources::MeshletTriangles,
                Builtin::MeshResources::MeshletVertexIndices,
                Builtin::MeshResources::MeshletOffsets,
                m_compactedVisibleClustersBuffer,
                m_rasterBucketsHistogramBuffer,
                m_viewRasterInfoBuffer)
                .WithIndirectArguments(m_rasterBucketsIndirectArgsBuffer)
                .IsGeometryPass();

            for (auto& vb : m_visibilityBuffers) { // Dynamic per-frame
                builder->WithUnorderedAccess(vb);
            }
        }

        void Setup() override {

            //RegisterSRV(Builtin::MeshResources::MeshletOffsets);
            RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
            RegisterSRV(Builtin::MeshResources::MeshletTriangles);

            RegisterSRV(Builtin::NormalMatrixBuffer);
            RegisterSRV(Builtin::PostSkinningVertices);
            RegisterSRV(Builtin::PerObjectBuffer);
            RegisterSRV(Builtin::CameraBuffer);
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerMeshBuffer);
            RegisterSRV(Builtin::PerMaterialDataBuffer);
            RegisterSRV(Builtin::MeshResources::MeshletOffsets);

            //RegisterSRV(Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer);
        }

        // Note: relies on Update() running before DeclareResourceUsages(). If this ever changes, we may need a new approach.
        void Update(const UpdateExecutionContext& executionContext) override {
            auto* updateContext = executionContext.hostData->Get<UpdateContext>();
            auto& context = *updateContext;

            // Build per-view raster metadata used by CLod mesh/pixel shaders.
            auto numViews = context.viewManager->GetCameraBufferSize();

            m_visibilityBuffers.clear();

            uint32_t maxViewWidth = 1;
            uint32_t maxViewHeight = 1;

            context.viewManager->ForEachView([&](uint64_t v) {
                auto viewInfo = context.viewManager->Get(v);
                if (viewInfo->gpu.visibilityBuffer != nullptr) {
                    maxViewWidth = std::max(maxViewWidth, viewInfo->gpu.visibilityBuffer->GetWidth());
                    maxViewHeight = std::max(maxViewHeight, viewInfo->gpu.visibilityBuffer->GetHeight());
                }
                });

            std::vector<CLodViewRasterInfo> viewRasterInfo(numViews);
            context.viewManager->ForEachView([&](uint64_t v) {
                auto viewInfo = context.viewManager->Get(v);
                if (viewInfo->gpu.visibilityBuffer != nullptr) {
                    auto cameraIndex = viewInfo->gpu.cameraBufferIndex;

                    CLodViewRasterInfo info{};
                    info.visibilityUAVDescriptorIndex = viewInfo->gpu.visibilityBuffer->GetUAVShaderVisibleInfo(0).slot.index;
                    info.scissorMinX = 0;
                    info.scissorMinY = 0;
                    info.scissorMaxX = viewInfo->gpu.visibilityBuffer->GetWidth();
                    info.scissorMaxY = viewInfo->gpu.visibilityBuffer->GetHeight();
                    info.viewportScaleX = static_cast<float>(info.scissorMaxX) / static_cast<float>(maxViewWidth);
                    info.viewportScaleY = static_cast<float>(info.scissorMaxY) / static_cast<float>(maxViewHeight);
                    viewRasterInfo[cameraIndex] = info;

                    m_visibilityBuffers.push_back(viewInfo->gpu.visibilityBuffer);
                }
                });

            m_passWidth = maxViewWidth;
            m_passHeight = maxViewHeight;

            // Check if per-view raster metadata changed
            if (m_viewRasterInfos != viewRasterInfo) {
                m_viewRasterInfos = viewRasterInfo;
                // Update the buffer
                m_viewRasterInfoBuffer->ResizeStructured(static_cast<uint32_t>(m_viewRasterInfos.size()));
                BUFFER_UPLOAD(
                    m_viewRasterInfos.data(),
                    static_cast<uint32_t>(m_viewRasterInfos.size() * sizeof(CLodViewRasterInfo)),
                    rg::runtime::UploadTarget::FromShared(m_viewRasterInfoBuffer),
                    0);
                m_declaredResourcesChanged = true;
            }
            else {
                m_declaredResourcesChanged = false;
            }
        }

        bool DeclaredResourcesChanged() const override {
            return m_declaredResourcesChanged;
        }

        PassReturn Execute(PassExecutionContext& executionContext) override {
            auto* renderContext = executionContext.hostData->Get<RenderContext>();
			auto& context = *renderContext;
            auto& commandList = executionContext.commandList;

            rhi::PassBeginInfo p{};
            p.width = m_passWidth;
            p.height = m_passHeight;
            p.debugName = "CLod raster pass";

            executionContext.commandList.BeginPass(p);

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);

            // Root signature
            commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());

            auto& psoManager = PSOManager::GetInstance();

            uint32_t misc[NumMiscUintRootConstants] = {};
            misc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_rasterBucketsHistogramBuffer->GetSRVInfo(0).slot.index;
            misc[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedVisibleClustersBuffer->GetSRVInfo(0).slot.index;
            misc[CLOD_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX] = m_viewRasterInfoBuffer->GetSRVInfo(0).slot.index;
            commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            if (numBuckets == 0) {
                return {};
            }

            auto apiResource = m_rasterBucketsIndirectArgsBuffer->GetAPIResource();
            // For each raster bucket, we have one ExecuteIndirect into a DispatchMesh command
            auto stride = sizeof(RasterizeClustersCommand);
            for (uint32_t i = 0; i < numBuckets; ++i) { // TODO: Compaction for zero-count buckets?

                auto flags = context.materialManager->GetRasterFlagsForBucket(i);
                auto& pso = psoManager.GetClusterLODRasterPSO(
                    flags,
                    m_wireframe);

                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

                const uint64_t argOffset = static_cast<uint64_t>(i) * stride;
                commandList.ExecuteIndirect(
                    m_rasterizationCommandSignature->GetHandle(),
                    apiResource.GetHandle(),
                    argOffset,
                    {},
                    0,
                    1);
            }

            return {};
        }

        void Cleanup() override {
        }

    private:

        bool m_wireframe;
        bool m_meshShaders;
        bool m_clearGbuffer = true;

        std::vector<CLodViewRasterInfo> m_viewRasterInfos;
        std::vector<std::shared_ptr<PixelBuffer>> m_visibilityBuffers;

        std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
        std::shared_ptr<Buffer> m_rasterBucketsHistogramBuffer;
        std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBuffer;

        rhi::CommandSignaturePtr m_rasterizationCommandSignature;

        std::shared_ptr<Buffer> m_viewRasterInfoBuffer;
        uint32_t m_passWidth = 1;
        uint32_t m_passHeight = 1;
        bool m_declaredResourcesChanged = true;

        RenderPhase m_renderPhase = Engine::Primary::GBufferPass;
    };

};