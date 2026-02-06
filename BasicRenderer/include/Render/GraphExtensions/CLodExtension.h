#pragma once

#include <rhi.h>

#include "Render/RenderGraph.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
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

        m_visibleClustersBuffer = CreateIndexedStructuredBuffer(maxVisibleClusters, sizeof(VisibleCluster), true, false);
		m_visibleClustersBuffer->SetName("CLod Visible Clusters Buffer");
		m_histogramIndirectCommand = CreateIndexedStructuredBuffer(1, sizeof(RasterBucketsHistogramIndirectCommand), true, false);
		m_histogramIndirectCommand->SetName("CLod Raster Buckets Histogram Indirect Command Buffer");
		m_rasterBucketsHistogramBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(1, "Raster bucket histogram", true);

        flecs::entity typeEntity;
		switch (type) {
            case CLodExtensionType::VisiblityBuffer:
				typeEntity = ecsWorld.entity<CLodExtensionVisibilityBufferTag>();
                break;
			case CLodExtensionType::Shadow:
				typeEntity = ecsWorld.entity<CLodExtensionShadowTag>();
				break;
		}

		// This tags the buffer with the extension type so passes can query for it with ECSResourceResolver
        m_visibleClustersBuffer->GetECSEntity()
           .set<Components::Resource>({ m_visibleClustersBuffer })
           .add<VisibleClustersBufferTag>()
           .add<CLodExtensionTypeTag>(typeEntity);
        m_visibleClustersCounterBuffer = CreateIndexedStructuredBuffer(1, sizeof(unsigned int), true, false);
		m_visibleClustersCounterBuffer->SetName("CLod Visible Clusters Counter Buffer");
		m_visibleClustersCounterBuffer->GetECSEntity()
            .set<Components::Resource>({ m_visibleClustersCounterBuffer })
			.add<VisibleClustersCounterTag>()
			.add<CLodExtensionTypeTag>(typeEntity);

        m_rasterBucketsOffsetsBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(1, "CLod Raster bucket offsets", true);
        m_rasterBucketsBlockSumsBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(1, "CLod Raster bucket block sums", true);
        m_rasterBucketsScannedBlockSumsBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(1, "CLod Raster bucket scanned block sums", true);
        m_rasterBucketsTotalCountBuffer = CreateIndexedStructuredBuffer(1, sizeof(uint32_t), true, false);
        m_rasterBucketsTotalCountBuffer->SetName("CLod Raster bucket total count");

		m_compactedVisibleClustersBuffer = CreateIndexedStructuredBuffer(maxVisibleClusters, sizeof(VisibleCluster), true, false);
		m_compactedVisibleClustersBuffer->SetName("CLod Compacted Visible Clusters Buffer");

		m_rasterBucketsWriteCursorBuffer = DynamicStructuredBuffer<uint32_t>::CreateShared(1, "CLod Raster bucket write cursor", true);
		m_rasterBucketsIndirectArgsBuffer = DynamicStructuredBuffer<RasterizeClustersCommand>::CreateShared(1, "CLod Raster bucket indirect args", true);

		m_type = type;
	}

	void OnRegistryReset(ResourceRegistry* reg) override {

	}

	void GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override {
		// Add the hierarchical culling pass
		RenderGraph::ExternalPassDesc cullPassDesc;
		cullPassDesc.type = RenderGraph::PassType::Compute;
		cullPassDesc.name = "CLod::HierarchialCullingPass";
		HierarchialCullingPassInputs cullPassInputs;
		cullPassInputs.isFirstPass = true; // For now, always true
		cullPassDesc.pass = std::make_shared<HierarchialCullingPass>(
            cullPassInputs, 
            m_visibleClustersBuffer, 
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand);
		cullPassDesc.where = RenderGraph::ExternalInsertPoint::After("SkinningPass");
		outPasses.push_back(std::move(cullPassDesc));

		RenderGraph::ExternalPassDesc histogramPassDesc;
		histogramPassDesc.type = RenderGraph::PassType::Compute;
		histogramPassDesc.name = "CLod::RasterBucketsHistogramPass";
        histogramPassDesc.pass = std::make_shared<RasterBucketHistogramPass>(
            m_visibleClustersBuffer, 
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBuffer);
		outPasses.push_back(std::move(histogramPassDesc));

        RenderGraph::ExternalPassDesc prefixScanPassDesc;
        prefixScanPassDesc.type = RenderGraph::PassType::Compute;
        prefixScanPassDesc.name = "CLod::RasterBucketsPrefixScanPass";
        prefixScanPassDesc.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer);
        outPasses.push_back(std::move(prefixScanPassDesc));

        RenderGraph::ExternalPassDesc prefixOffsetsPassDesc;
        prefixOffsetsPassDesc.type = RenderGraph::PassType::Compute;
        prefixOffsetsPassDesc.name = "CLod::RasterBucketsPrefixOffsetsPass";
        prefixOffsetsPassDesc.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer);
        outPasses.push_back(std::move(prefixOffsetsPassDesc));

        RenderGraph::ExternalPassDesc compactPassDesc;
        compactPassDesc.type = RenderGraph::PassType::Compute;
        compactPassDesc.name = "CLod::RasterBucketsCompactAndArgsPass";
        compactPassDesc.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
			m_histogramIndirectCommand, // Reused for cluster compaction
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsWriteCursorBuffer,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_maxVisibleClusters);
        outPasses.push_back(std::move(compactPassDesc));

		RenderGraph::ExternalPassDesc rasterizePassDesc;
		rasterizePassDesc.type = RenderGraph::PassType::Render;
        rasterizePassDesc.name = "CLod::RasterizeClustersPass";
		ClusterRasterizationPassInputs rasterizePassInputs;
        rasterizePassInputs.clearGbuffer = true;
        rasterizePassInputs.wireframe = false;
        rasterizePassDesc.pass = std::make_shared<ClusterRasterizationPass>(
			rasterizePassInputs,
            m_compactedVisibleClustersBuffer,
			m_rasterBucketsHistogramBuffer,
			m_rasterBucketsIndirectArgsBuffer);
		outPasses.push_back(std::move(rasterizePassDesc));
	}

private:

	CLodExtensionType m_type;
	uint64_t m_maxVisibleClusters;

	// Buffers used across CLod passes
    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;

    // Histogram Pass Buffers
    std::shared_ptr<Buffer> m_histogramIndirectCommand;
    std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_rasterBucketsHistogramBuffer;

	// prefix scan buffers
    std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_rasterBucketsOffsetsBuffer;
    std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_rasterBucketsBlockSumsBuffer;
    std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_rasterBucketsScannedBlockSumsBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsTotalCountBuffer;

	// Compaction Pass Buffers
    std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
    std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_rasterBucketsWriteCursorBuffer;
    std::shared_ptr<DynamicStructuredBuffer<RasterizeClustersCommand>> m_rasterBucketsIndirectArgsBuffer;

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

    class HierarchialCullingPass : public ComputePass {
    public:
        HierarchialCullingPass(
            HierarchialCullingPassInputs inputs, 
            std::shared_ptr<Buffer> visibleClustersBuffer, 
            std::shared_ptr<Buffer> visibleClustersCounterBuffer,
            std::shared_ptr<Buffer> histogramIndirectCommand) {
            CreatePipelines(
                DeviceManager::GetInstance().GetDevice(),
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_workGraph,
                m_createCommandPipelineState);
            auto memSize = m_workGraph->GetRequiredScratchMemorySize();
            m_scratchBuffer = Buffer::CreateShared( // TODO: Make a way for the graph to provide things like this, to allow for aliasing
                rhi::HeapType::DeviceLocal,
                memSize,
                true);
            m_scratchBuffer->ApplyMetadataComponentBundle(
                EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Work graph scratch buffer" }));
			m_visibleClustersBuffer = visibleClustersBuffer;
			m_visibleClustersCounterBuffer = visibleClustersCounterBuffer;
            m_histogramIndirectCommand = histogramIndirectCommand;
        }

        ~HierarchialCullingPass() {
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) {
            auto ecsWorld = ECSManager::GetInstance().GetWorld();
            flecs::query<> drawSetIndicesQuery = ecsWorld.query_builder<>()
                .with<Components::IsActiveDrawSetIndices>()
                .with<Components::ParticipatesInPass>(flecs::Wildcard)
                .build();
            builder->WithUnorderedAccess(m_scratchBuffer,
                m_visibleClustersBuffer,
                m_visibleClustersCounterBuffer,
                m_histogramIndirectCommand)
                .WithShaderResource(Builtin::IndirectCommandBuffers::Master,
                    Builtin::CLod::Offsets,
                    Builtin::CLod::Groups,
                    Builtin::CLod::Children,
                    Builtin::CLod::ChildLocalMeshletIndices,
                    Builtin::CLod::Nodes,
                    Builtin::CullingCameraBuffer,
                    Builtin::PerMeshInstanceBuffer,
                    Builtin::PerObjectBuffer)
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

            //RegisterUAV(Builtin::VisibleClusterBuffer);
            //RegisterUAV(Builtin::VisibleClusterCounter);

            //m_visibleClusterCounter = m_resourceRegistryView->RequestHandle(Builtin::VisibleClusterCounter);
        }

        PassReturn Execute(RenderContext& context) override {
            auto& commandList = context.commandList;

            // Set the descriptor heaps
            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

            std::vector<ObjectCullRecord> cullRecords;

            // Build cull records for all indirect buffers
			// TODO: Non-opaque objects, and non-primary cameras
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
                    // Calculate dispatch grid size (assuming 64 threads per group)
                    record.dispatchGridX = static_cast<uint>((count + 63) / 64);
                    record.dispatchGridY = 1;
                    record.dispatchGridZ = 1;
					cullRecords.push_back(record);
                }
                
                });

            commandList.SetWorkGraph(m_workGraph->GetHandle(), m_scratchBuffer->GetAPIResource().GetHandle(), true); // Reset every time for now

            BindResourceDescriptorIndices(commandList, m_pipelineResources);

            uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            uintRootConstants[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
			uintRootConstants[CLOD_RASTER_BUCKET_HISTOGRAM_COMMAND_DESCRIPTOR_INDEX] = m_histogramIndirectCommand->GetUAVShaderVisibleInfo(0).slot.index;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                uintRootConstants);

            rhi::WorkGraphDispatchDesc dispatchDesc{};
            dispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::NodeCpuInput;
            dispatchDesc.nodeCpuInput.entryPointIndex = 0; // ObjectCull node
            dispatchDesc.nodeCpuInput.pRecords = cullRecords.data();
            dispatchDesc.nodeCpuInput.numRecords = static_cast<uint32_t>(cullRecords.size());
            dispatchDesc.nodeCpuInput.recordByteStride = sizeof(ObjectCullRecord);

            // Builds list of visible clusters
            commandList.DispatchWorkGraph(dispatchDesc);

            // UAV barrier on the visible cluster counter
            rhi::BufferBarrier barrier{};
            barrier.buffer = m_visibleClustersCounterBuffer->GetAPIResource().GetHandle();
            barrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
            barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            barrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
            barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
            rhi::BarrierBatch bufferBarriers{};
            bufferBarriers.buffers = rhi::Span<rhi::BufferBarrier>(&barrier, 1);
            commandList.Barriers(bufferBarriers);

			// Create indirect command buffer for cluster histogram
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
            commandList.Dispatch(1, 1, 1); // Single thread group, one thread

            return {};
        }

        void Update(const UpdateContext& context) override {
            uint32_t zero = 0u;
            BUFFER_UPLOAD(&zero, sizeof(uint32_t), UploadManager::UploadTarget::FromShared(m_visibleClustersCounterBuffer), 0);
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
            std::array<rhi::ShaderExportDesc, 5> exports = { {
                { "WG_ObjectCull",   nullptr },
                { "WG_Traverse",     nullptr },
                { "WG_ClusterCullBuckets",  nullptr },
                //{ "WG_Output", nullptr }
            } };

            rhi::ShaderLibraryDesc library{};
            library.dxil = libDxil;
            library.exports = rhi::Span<rhi::ShaderExportDesc>(exports.data(), static_cast<uint32_t>(exports.size()));

            std::array<rhi::ShaderLibraryDesc, 1> libraries = { library };

            // Entry point is by NodeID (the [NodeID("ObjectCull")] in HLSL)
            std::array<rhi::NodeIDDesc, 1> entrypoints = { {
                { "ObjectCull", 0 }
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
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> histogramBuffer) {
            CreatePipelines(
                DeviceManager::GetInstance().GetDevice(),
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_histogramPipeline);

            // Used by the cluster rasterization pass
            rhi::IndirectArg rasterizeClustersArgs[] = {
                {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { MiscUintRootSignatureIndex, 0, 2 } } },
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

        void DeclareResourceUsages(ComputePassBuilder* builder) {
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

        PassReturn Execute(RenderContext& context) override {
            auto& commandList = context.commandList;

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

        void Update(const UpdateContext& context) override {
			auto numRasterBuckets = context.materialManager->GetRasterBucketCount();

			// Resize histogram buffer if needed
            if (m_histogramBuffer->Size() < numRasterBuckets) {
                m_histogramBuffer->Resize(numRasterBuckets);
            }

            // Clear the histogram buffer
			std::vector<uint32_t> zeroData(numRasterBuckets, 0);
			BUFFER_UPLOAD(zeroData.data(), static_cast<uint32_t>(zeroData.size() * sizeof(uint32_t)), UploadManager::UploadTarget::FromShared(m_histogramBuffer), 0);
        }

        void Cleanup() override {

        }

    private:
        PipelineState m_histogramPipeline;
        rhi::CommandSignaturePtr m_histogramCommandSignature;
		std::shared_ptr<Buffer> m_visibleClustersBuffer;
		std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
		std::shared_ptr<Buffer> m_histogramIndirectCommand;
		std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_histogramBuffer;

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
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> histogramBuffer,
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> offsetsBuffer,
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> blockSumsBuffer)
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

        PassReturn Execute(RenderContext& context) override {
            auto& commandList = context.commandList;
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

        void Update(const UpdateContext& context) override {
            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            if (m_offsetsBuffer->Size() < numBuckets) {
                m_offsetsBuffer->Resize(numBuckets);
            }
            if (m_blockSumsBuffer->Size() < numBlocks) {
                m_blockSumsBuffer->Resize(numBlocks);
            }
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
        uint32_t m_blockSize = 1024;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_histogramBuffer;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_offsetsBuffer;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_blockSumsBuffer;
    };

    class RasterBucketBlockOffsetsPass : public ComputePass {
    public:
        RasterBucketBlockOffsetsPass(
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> offsetsBuffer,
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> blockSumsBuffer,
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> scannedBlockSumsBuffer,
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

        PassReturn Execute(RenderContext& context) override {
            auto& commandList = context.commandList;
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

        void Update(const UpdateContext& context) override {
            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

            if (m_scannedBlockSumsBuffer->Size() < numBlocks) {
                m_scannedBlockSumsBuffer->Resize(numBlocks);
            }
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
        uint32_t m_blockSize = 1024;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_offsetsBuffer;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_blockSumsBuffer;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_scannedBlockSumsBuffer;
        std::shared_ptr<Buffer> m_totalCountBuffer;
    };

    class RasterBucketCompactAndArgsPass : public ComputePass {
    public:
        RasterBucketCompactAndArgsPass(
            std::shared_ptr<Buffer> visibleClustersBuffer,
            std::shared_ptr<Buffer> visibleClustersCounterBuffer,
            std::shared_ptr<Buffer> indirectCommand,
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> histogramBuffer,
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> offsetsBuffer,
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> writeCursorBuffer,
            std::shared_ptr<Buffer> compactedClustersBuffer,
            std::shared_ptr<DynamicStructuredBuffer<RasterizeClustersCommand>> indirectArgsBuffer,
            uint64_t maxVisibleClusters)
            : m_visibleClustersBuffer(std::move(visibleClustersBuffer)),
            m_visibleClustersCounterBuffer(std::move(visibleClustersCounterBuffer)),
			m_indirectCommand(std::move(indirectCommand)),
            m_histogramBuffer(std::move(histogramBuffer)),
            m_offsetsBuffer(std::move(offsetsBuffer)),
            m_writeCursorBuffer(std::move(writeCursorBuffer)),
            m_compactedClustersBuffer(std::move(compactedClustersBuffer)),
            m_indirectArgsBuffer(std::move(indirectArgsBuffer)),
            m_maxVisibleClusters(maxVisibleClusters)
        {
            m_pso = PSOManager::GetInstance().MakeComputePipeline(
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                L"shaders/ClusterLOD/clodUtil.hlsl",
                L"CompactClustersAndBuildIndirectArgsCS",
                {},
                "CLod_RasterBucketsCompactAndArgsPSO");

            rhi::IndirectArg args[] = {
                {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { MiscUintRootSignatureIndex, 0, 2 } } },
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
                m_histogramBuffer,
                m_offsetsBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerMeshBuffer,
                Builtin::PerMaterialDataBuffer)
                .WithUnorderedAccess(
                    m_writeCursorBuffer,
                    m_compactedClustersBuffer,
                    m_indirectArgsBuffer);
        }

        void Setup() override {
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerMeshBuffer);
            RegisterSRV(Builtin::PerMaterialDataBuffer);
        }

        PassReturn Execute(RenderContext& context) override {
            auto& commandList = context.commandList;
            auto& pm = PSOManager::GetInstance();

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            const uint32_t kThreads = 64;
            const uint64_t maxItems = std::max<uint64_t>(m_maxVisibleClusters, numBuckets);
            const uint32_t groups = static_cast<uint32_t>((maxItems + kThreads - 1u) / kThreads);

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
            commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());
            commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

            uint32_t rc[NumMiscUintRootConstants] = {};
            rc[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetSRVInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_WRITE_CURSOR_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_RASTER_BUCKETS_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rc[CLOD_NUM_RASTER_BUCKETS] = numBuckets;
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

        void Update(const UpdateContext& context) override {
            auto numBuckets = context.materialManager->GetRasterBucketCount();

            if (m_writeCursorBuffer->Size() < numBuckets) {
                m_writeCursorBuffer->Resize(numBuckets);
            }
            if (m_indirectArgsBuffer->Size() < numBuckets) {
                m_indirectArgsBuffer->Resize(numBuckets);
            }

            std::vector<uint32_t> zeroData(numBuckets, 0u);
            BUFFER_UPLOAD(zeroData.data(),
                static_cast<uint32_t>(zeroData.size() * sizeof(uint32_t)),
                UploadManager::UploadTarget::FromShared(m_writeCursorBuffer),
                0);
        }

        void Cleanup() override {}

    private:
        PipelineState m_pso;
        rhi::CommandSignaturePtr m_compactionCommandSignature;

        std::shared_ptr<Buffer> m_visibleClustersBuffer;
        std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
		std::shared_ptr<Buffer> m_indirectCommand;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_histogramBuffer;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_offsetsBuffer;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_writeCursorBuffer;
        std::shared_ptr<Buffer> m_compactedClustersBuffer;
        std::shared_ptr<DynamicStructuredBuffer<RasterizeClustersCommand>> m_indirectArgsBuffer;

        uint64_t m_maxVisibleClusters = 0;
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

    class ClusterRasterizationPass : public RenderPass {
    public:
        ClusterRasterizationPass(
			ClusterRasterizationPassInputs inputs,
            std::shared_ptr<Buffer> compactedVisibleClustersBuffer,
            std::shared_ptr<DynamicStructuredBuffer<uint32_t>> rasterBucketsHistogramBuffer,
            std::shared_ptr<DynamicStructuredBuffer<RasterizeClustersCommand>> rasterBucketsIndirectArgsBuffer)
            : m_compactedVisibleClustersBuffer(std::move(compactedVisibleClustersBuffer)),
            m_rasterBucketsHistogramBuffer(std::move(rasterBucketsHistogramBuffer)),
            m_rasterBucketsIndirectArgsBuffer(std::move(rasterBucketsIndirectArgsBuffer)) {
            m_wireframe = inputs.wireframe;
            m_clearGbuffer = inputs.clearGbuffer;
        }

        ~ClusterRasterizationPass() {
        }

        void DeclareResourceUsages(RenderPassBuilder* builder) {

            builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS,
                Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer,
                Builtin::PerObjectBuffer,
                Builtin::NormalMatrixBuffer,
                Builtin::PerMeshBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerMaterialDataBuffer,
                Builtin::PostSkinningVertices,
                Builtin::CameraBuffer,
                m_compactedVisibleClustersBuffer, 
                m_rasterBucketsHistogramBuffer)
                .WithRenderTarget(
                    Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 })
                )
                .WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
                .WithRenderTarget(
                Builtin::PrimaryCamera::VisibilityTexture)
                .WithIndirectArguments(m_rasterBucketsIndirectArgsBuffer)
                .IsGeometryPass();

        }

        void Setup() override {

            m_pPrimaryDepthBuffer = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
            m_pVisibilityBuffer = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::VisibilityTexture);

            if (m_meshShaders) {
                RegisterSRV(Builtin::MeshResources::MeshletOffsets);
                RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
                RegisterSRV(Builtin::MeshResources::MeshletTriangles);
            }

            RegisterSRV(Builtin::NormalMatrixBuffer);
            RegisterSRV(Builtin::PostSkinningVertices);
            RegisterSRV(Builtin::PerObjectBuffer);
            RegisterSRV(Builtin::CameraBuffer);
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerMeshBuffer);
            RegisterSRV(Builtin::PerMaterialDataBuffer);
            RegisterSRV(Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer);
        }

        PassReturn Execute(RenderContext& context) override {
            auto& commandList = context.commandList;

            BeginPass(context);

            SetupCommonState(context, commandList);
            SetCommonRootConstants(context, commandList);

            ExecuteMeshShaderIndirect(context, commandList);

            return {};
        }

        void Cleanup() override {
        }

    private:
        void BeginPass(RenderContext& context) {
            // Build attachments
            rhi::PassBeginInfo p{};
            p.width = context.renderResolution.x;
            p.height = context.renderResolution.y;
            p.debugName = "GBuffer Pass";

            rhi::DepthAttachment da{};
            da.dsv = m_pPrimaryDepthBuffer->GetDSVInfo(0).slot;
            da.depthStore = rhi::StoreOp::Store;

            if (m_clearGbuffer) {
                da.depthLoad = rhi::LoadOp::Clear;
                da.clear.type = rhi::ClearValueType::DepthStencil;
                da.clear.format = rhi::Format::D32_Float;
                da.clear.depthStencil.depth = 1.0f;
                da.clear.depthStencil.stencil = 0;
            }
            else {
                da.depthLoad = rhi::LoadOp::Load;
            }
            p.depth = &da;

            std::vector<rhi::ColorAttachment> colors;

            // visibility buffer
            {
                rhi::ColorAttachment ca{};
                ca.rtv = m_pVisibilityBuffer->GetRTVInfo(0).slot;
                ca.storeOp = rhi::StoreOp::Store;
                ca.loadOp = rhi::LoadOp::Load; // Clearing is handled in a separate pass
                colors.push_back(ca);
            }

            p.colors = { colors.data(), (uint32_t)colors.size() };

            context.commandList.BeginPass(p);
        }
        // Common setup code that doesn't change between techniques
        void SetupCommonState(RenderContext& context, rhi::CommandList& commandList) {

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);

            // Root signature
            commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
        }

        void SetCommonRootConstants(RenderContext& context, rhi::CommandList& commandList) {

        }

        void ExecuteMeshShaderIndirect(RenderContext& context, rhi::CommandList& commandList) {
            auto& psoManager = PSOManager::GetInstance();
            auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

            uint32_t misc[NumMiscUintRootConstants] = {};
            misc[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_rasterBucketsHistogramBuffer->GetSRVInfo(0).slot.index;
            misc[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedVisibleClustersBuffer->GetSRVInfo(0).slot.index;
            commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);

            auto numBuckets = context.materialManager->GetRasterBucketCount();
            if (numBuckets == 0) {
                return;
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
                    commandSignature.GetHandle(),
                    apiResource.GetHandle(),
                    argOffset,
                    {},
                    0,
                    1);
            }
        }

    private:

        bool m_wireframe;
        bool m_meshShaders;
        bool m_clearGbuffer = true;

        PixelBuffer* m_pPrimaryDepthBuffer;
        PixelBuffer* m_pVisibilityBuffer;
        std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
        std::shared_ptr<DynamicStructuredBuffer<uint32_t>> m_rasterBucketsHistogramBuffer;
        std::shared_ptr<DynamicStructuredBuffer<RasterizeClustersCommand>> m_rasterBucketsIndirectArgsBuffer;

        RenderPhase m_renderPhase = Engine::Primary::GBufferPass;
    };

};
