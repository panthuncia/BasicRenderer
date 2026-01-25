#pragma once

#include <rhi_debug.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include <boost/container_hash/hash.hpp>

struct ObjectCullRecord
{
    uint viewDataIndex; // One record per view *...
    uint activeDrawSetIndicesSRVIndex; // One record per draw set
    uint activeDrawCount;
	uint pad0; // Padding for 16-byte alignment
    uint dispatchGridX; // Drives dispatch size
	uint dispatchGridY;
	uint dispatchGridZ;
	uint pad1; // Padding for 16-byte alignment
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
    HierarchialCullingPass(HierarchialCullingPassInputs inputs) {
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
            Builtin::VisibleClusterBuffer,
            Builtin::VisibleClusterCounter,
            Builtin::RasterizeClustersIndirectCommand)
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

		RegisterUAV(Builtin::VisibleClusterBuffer);
		RegisterUAV(Builtin::VisibleClusterCounter);
		RegisterUAV(Builtin::RasterizeClustersIndirectCommand);

		m_visibleClusterCounter = m_resourceRegistryView->RequestHandle(Builtin::VisibleClusterCounter);
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

		// Set the descriptor heaps
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

		std::vector<ObjectCullRecord> cullRecords;

		// Build cull records for all indirect buffers
        context.indirectCommandBufferManager->ForEachIndirectBuffer([&](uint64_t view,
            MaterialCompileFlags flags,
            const IndirectWorkload& wl)
            {
                if (wl.count == 0) {
                    return;
                }
                auto viewInfo = context.viewManager->Get(view);
                auto cameraBufferIndex = viewInfo->gpu.cameraBufferIndex;
				ObjectCullRecord record{};
				record.viewDataIndex = cameraBufferIndex;
				record.activeDrawSetIndicesSRVIndex = context.objectManager->GetActiveDrawSetIndices(flags)->GetSRVInfo(0).slot.index;
				record.activeDrawCount = wl.count;
				// Calculate dispatch grid size (assuming 64 threads per group)
				record.dispatchGridX = static_cast<uint>((wl.count + 63) / 64);
				record.dispatchGridY = 1;
				record.dispatchGridZ = 1;
				cullRecords.push_back(record);
			});

		commandList.SetWorkGraph(m_workGraph->GetHandle(), m_scratchBuffer->GetAPIResource().GetHandle(), true); // Reset every time for now

        BindResourceDescriptorIndices(commandList, m_pipelineResources);

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
        barrier.buffer = m_resourceRegistryView->Resolve<Resource>(m_visibleClusterCounter)->GetAPIResource().GetHandle();
        barrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        barrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
		barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
		rhi::BarrierBatch bufferBarriers{};
		bufferBarriers.buffers = rhi::Span<rhi::BufferBarrier>(&barrier, 1);
		commandList.Barriers(bufferBarriers);

		// Create indirect command buffer for LOD rasterization
		BindResourceDescriptorIndices(commandList, m_createCommandPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_createCommandPipelineState.GetAPIPipelineState().GetHandle());
		commandList.Dispatch(1, 1, 1); // Single thread group, one thread

		return {};
	}

	void Update(const UpdateContext& context) override {
        uint32_t zero = 0u;
		BUFFER_UPLOAD(&zero, sizeof(uint32_t), UploadManager::UploadTarget::FromHandle(m_visibleClusterCounter), 0);
    }

	void Cleanup() override {

	}

private:
	PipelineResources m_pipelineResources;
	rhi::WorkGraphPtr m_workGraph;
	PipelineState m_createCommandPipelineState;
	std::shared_ptr<Buffer> m_scratchBuffer;
    ResourceRegistry::RegistryHandle m_visibleClusterCounter;

    rhi::Result CreatePipelines(
        rhi::Device device,
        rhi::PipelineLayoutHandle globalRootSignature,
        rhi::WorkGraphPtr& outGraph,
        PipelineState& outCreateCommandPipeline)
    {
        // Compile the work-graph library
		ShaderLibraryInfo libInfo(L"shaders/workGraphCulling.hlsl", L"lib_6_8");
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
        return device.CreateWorkGraph(wg, outGraph);

		// Pipeline to create indirect command
        outCreateCommandPipeline = PSOManager::GetInstance().MakeComputePipeline(
            globalRootSignature,
            L"shaders/createRasterizeClustersCommand.hlsl",
            L"CreateRasterizeClustersCommand",
            {},
			"HierarchialLODRasterizeCommandCreation");
    }
};