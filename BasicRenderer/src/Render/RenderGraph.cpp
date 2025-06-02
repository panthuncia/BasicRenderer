#include "Render/RenderGraph.h"

#include <span>
#include <ThirdParty/pix/pix3.h>

#include "Render/RenderContext.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/ReadbackManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Render/PassBuilders.h"
#include "Resources/ResourceGroup.h"
#include "Managers/Singletons/StatisticsManager.h"

// Factory for the transition lambda
void RenderGraph::AddTransition(
	CompileContext& context,
	unsigned int batchIndex,
	PassBatch& currentBatch,
	bool isComputePass, 
	const ResourceRequirement& r)
{
	auto& resource = r.resourceAndRange.resource;

	std::vector<ResourceTransition> transitions;
	resource->GetStateTracker()->Apply(r.resourceAndRange.range, resource.get(), r.state, transitions);

	currentBatch.passBatchTrackers[resource->GetGlobalResourceID()] = resource->GetStateTracker(); // We will need to chack subsequent passes against this

	// Check if this is a resource group
	//std::vector<ResourceTransition> independantlyManagedTransitions;
	auto group = std::dynamic_pointer_cast<ResourceGroup>(resource);
	if (group) {
		for (auto& childID : resourcesFromGroupToManageIndependantly[group->GetGlobalResourceID()]) {
			auto& child = resourcesByID[childID];
			if (child) {
				currentBatch.passBatchTrackers[childID] = child->GetStateTracker();
				child->GetStateTracker()->Apply(r.resourceAndRange.range, child.get(), r.state, transitions);
			} else {
				spdlog::error("Resource group {} has a child resource {} that is marked as independantly managed, but is not managed by this graph. This should not happen.", group->GetGlobalResourceID(), childID);
				throw(std::runtime_error("Resource group has a child resource that is not managed by this graph"));
			}
		}
	}

	bool oldSyncHasNonComputeSyncState = false;
	for (auto& transition : transitions) {
		if (ResourceSyncStateIsNotComputeSyncState(transition.prevSyncState)) {
			oldSyncHasNonComputeSyncState = true;
		}
	}
	if (isComputePass && oldSyncHasNonComputeSyncState) { // We need to palce transitions on render queue
		unsigned int gfxBatch = context.transHistRender[resource->GetGlobalResourceID()];
		for (auto& transition : transitions) {
			context.transHistRender[transition.pResource->GetGlobalResourceID()] = gfxBatch;
			batches[gfxBatch].passEndTransitions.push_back(transition);
		}
	}
	else {
		if (isComputePass) {
			for (auto& transition : transitions) {
				context.transHistCompute[transition.pResource->GetGlobalResourceID()] = batchIndex;
				currentBatch.computeTransitions.push_back(transition);
			}
		}
		else {
			for (auto& transition : transitions) {
				context.transHistRender[transition.pResource->GetGlobalResourceID()] = batchIndex;
				currentBatch.renderTransitions.push_back(transition);
			}
		}
	}
}

void RenderGraph::ProcessResourceRequirements(
	bool isCompute,
	std::vector<ResourceRequirement>& resourceRequirements,
	CompileContext& compileContext,
	std::unordered_map<uint64_t, unsigned int>& producerHistory,
	unsigned int batchIndex,
	PassBatch& currentBatch) {

	for (auto& resourceRequirement : resourceRequirements) {

		if (!resourcesByID.contains(resourceRequirement.resourceAndRange.resource->GetGlobalResourceID())) {
			spdlog::error("Resource referenced by pass is not managed by this graph");
			throw(std::runtime_error("Resource referenced is not managed by this graph"));
		}

		if (resourceRequirement.state.access & D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ && resourceRequirement.state.layout == ResourceLayout::LAYOUT_SHADER_RESOURCE) {
			spdlog::error("Resource {} has depth stencil read access but is in shader resource layout");
		}
		const auto& id = resourceRequirement.resourceAndRange.resource->GetGlobalResourceID();

		AddTransition(compileContext, batchIndex, currentBatch, isCompute, resourceRequirement);

		if (AccessTypeIsWriteType(resourceRequirement.state.access)) {
			if (resourcesFromGroupToManageIndependantly.contains(id)) { // This is a resource group, and we may be transitioning some children independantly
				for (auto& childID : resourcesFromGroupToManageIndependantly[id]) {
					producerHistory[childID] = batchIndex;
				}
			}
			producerHistory[id] = batchIndex;
		}
	}
}

void RenderGraph::Compile() {
    batches.clear();

	//Check if any of the resource groups we have have Nth children that wa also manage directly
	for (auto& resourceGroup : resourceGroups) {
		auto children = resourceGroup->GetChildIDs();
		for (auto& childID : children) {
			if (resourcesByID.contains(childID)) {
				resourceGroup->MarkResourceAsNonStandard(resourcesByID[childID]);
				if (!resourcesFromGroupToManageIndependantly.contains(resourceGroup->GetGlobalResourceID())) {
					resourcesFromGroupToManageIndependantly[resourceGroup->GetGlobalResourceID()] = {};
				}
				resourcesFromGroupToManageIndependantly[resourceGroup->GetGlobalResourceID()].push_back(childID);
			}
		}
	}

	// Manage aliased resources 

	// Mark resources that use the same memory as each other, as they need aliasing barriers
	for (auto& resource : resourcesByID) {
		auto& r = resource.second;
		for (auto& alias : r->GetAliasedResources()) {
			if (!aliasedResources.contains(r->GetGlobalResourceID())) {
				aliasedResources[r->GetGlobalResourceID()] = {};
			}
			aliasedResources[r->GetGlobalResourceID()].insert(alias->GetGlobalResourceID());
		}
	}
	// Assemble alias groups
	{
		std::unordered_set<uint64_t> visited;
		for (auto& [rID, aliases] : aliasedResources) {
			if (visited.count(rID)) continue;
			// BFS-connected component
			std::vector<uint64_t> group;
			std::queue<uint64_t>  queue;
			queue.push(rID);
			visited.insert(rID);
			while (!queue.empty()) {
				uint64_t cur = queue.front(); queue.pop();
				group.push_back(cur);
				for (uint64_t other : aliasedResources[cur]) {
					if (!visited.count(other)) {
						visited.insert(other);
						queue.push(other);
					}
				}
			}
			size_t idx = aliasGroups.size();
			for (uint64_t member : group)
				resourceToAliasGroup[member] = idx;
			aliasGroups.push_back(std::move(group));
		}
	}

	lastActiveSubresourceInAliasGroup.clear();
	lastActiveSubresourceInAliasGroup.resize(aliasGroups.size());

    auto currentBatch = PassBatch();
    currentBatch.renderTransitionFenceValue = GetNextGraphicsQueueFenceValue();
    currentBatch.renderCompletionFenceValue = GetNextGraphicsQueueFenceValue();
    currentBatch.computeTransitionFenceValue = GetNextComputeQueueFenceValue();
    currentBatch.computeCompletionFenceValue = GetNextComputeQueueFenceValue();
    //std::unordered_map<std::wstring, ResourceState> previousBatchResourceStates;
 //   std::unordered_map<uint64_t, ResourceAccessType> finalResourceAccessTypes;
	//std::unordered_map<uint64_t, ResourceLayout> finalResourceLayouts;
	//std::unordered_map<uint64_t, ResourceSyncState> finalResourceSyncStates;

	//std::unordered_map<uint64_t, ResourceAccessType> firstResourceAccessTypes;
	//std::unordered_map<uint64_t, ResourceLayout> firstResourceLayouts;
	//std::unordered_map<uint64_t, ResourceSyncState> firstResourceSyncStates;

	std::unordered_set<uint64_t> computeUAVs;
	std::unordered_set<uint64_t> renderUAVs;

    std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueTransition;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueTransition;

	std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueProducer;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueProducer;

	CompileContext context;

	unsigned int currentBatchIndex = 0;
    for (auto& pr : passes) {


		bool isCompute = (pr.type == PassType::Compute);

		if (isCompute) {
			auto& pass = std::get<ComputePassAndResources>(pr.pass);
			if (IsNewBatchNeeded(pass.resources.resourceRequirements, currentBatch.passBatchTrackers, renderUAVs)) {
				//for (auto& [id, st] : currentBatch.resourceAccessTypes)
				//	finalResourceAccessTypes[id] = st;
				//for (auto& [id, st] : currentBatch.resourceLayouts)
				//	finalResourceLayouts[id] = st;
				batches.push_back(std::move(currentBatch));
				currentBatch = PassBatch();
				currentBatch.renderTransitionFenceValue   = GetNextGraphicsQueueFenceValue();
				currentBatch.renderCompletionFenceValue   = GetNextGraphicsQueueFenceValue();
				currentBatch.computeTransitionFenceValue  = GetNextComputeQueueFenceValue();
				currentBatch.computeCompletionFenceValue  = GetNextComputeQueueFenceValue();
				++currentBatchIndex;
			}
		} else {
			auto& pass = std::get<RenderPassAndResources>(pr.pass);
			if (IsNewBatchNeeded(pass.resources.resourceRequirements, currentBatch.passBatchTrackers, computeUAVs)) {
				//for (auto& [id, st] : currentBatch.resourceAccessTypes)
				//	finalResourceAccessTypes[id] = st;
				//for (auto& [id, st] : currentBatch.resourceLayouts)
				//	finalResourceLayouts[id] = st;
				batches.push_back(std::move(currentBatch));
				currentBatch = PassBatch();
				currentBatch.renderTransitionFenceValue   = GetNextGraphicsQueueFenceValue();
				currentBatch.renderCompletionFenceValue   = GetNextGraphicsQueueFenceValue();
				currentBatch.computeTransitionFenceValue  = GetNextComputeQueueFenceValue();
				currentBatch.computeCompletionFenceValue  = GetNextComputeQueueFenceValue();
				++currentBatchIndex;
			}
		}

		// dispatch categories
		if (isCompute) {
			auto& pass = std::get<ComputePassAndResources>(pr.pass);
			ProcessResourceRequirements(
				isCompute,
				pass.resources.resourceRequirements,
				context,
				batchOfLastComputeQueueTransition,
				currentBatchIndex,
				currentBatch);
			currentBatch.computePasses.push_back(pass);
			//UpdateDesiredResourceStates(currentBatch, pass, computeUAVs);
		} else {
			auto& pass = std::get<RenderPassAndResources>(pr.pass);
			ProcessResourceRequirements(
				isCompute,
				pass.resources.resourceRequirements,
				context,
				batchOfLastRenderQueueTransition,
				currentBatchIndex,
				currentBatch);
			currentBatch.renderPasses.push_back(pass);
			//UpdateDesiredResourceStates(currentBatch, pass, renderUAVs);
		}

		if (isCompute) {
			applySynchronization(
				/*isComputePass=*/true,
				currentBatch,
				currentBatchIndex,
				std::get<ComputePassAndResources>(pr.pass),
				batchOfLastRenderQueueTransition,
				batchOfLastRenderQueueProducer
			);
		} else {
			applySynchronization(
				/*isComputePass=*/false,
				currentBatch,
				currentBatchIndex,
				std::get<RenderPassAndResources>(pr.pass),
				batchOfLastComputeQueueTransition,
				batchOfLastComputeQueueProducer
			);
		}
    }

    // Handle the last batch
    //ComputeTransitionsForBatch(currentBatch, finalResourceStates);
 //   for (const auto& [resourceID, accessType] : currentBatch.resourceAccessTypes) {
 //       finalResourceAccessTypes[resourceID] = accessType;
 //   }
	//for (const auto& [resourceID, layout] : currentBatch.resourceLayouts) {
	//	finalResourceLayouts[resourceID] = layout;
	//}
    batches.push_back(std::move(currentBatch));

    // Insert transitions to loop resources back to their initial states
	ComputeResourceLoops();

	// Readback pass in its own batch
	auto readbackPass = ReadbackManager::GetInstance().GetReadbackPass();
	if (readbackPass) {
		auto readbackBatch = PassBatch();
		RenderPassAndResources readbackPassAndResources; // ReadbackPass is a special-case pass which transitions resources internally
		readbackPassAndResources.pass = readbackPass;
		readbackBatch.renderPasses.push_back(readbackPassAndResources);
		batches.push_back(readbackBatch);
	}
}

std::pair<int, int> RenderGraph::GetBatchesToWaitOn(
	const ComputePassAndResources& pass,
	std::unordered_map<uint64_t, unsigned int> const& transitionHistory,
	std::unordered_map<uint64_t, unsigned int> const& producerHistory)
{
	int latestTransition = -1, latestProducer = -1;

	auto processResource = [&](Resource* const& res) {
		uint64_t id = res->GetGlobalResourceID();
		// get this ID plus any aliases
		auto ids = GetAllAliasIDs(id);
		for (auto rid : ids) {
			auto itT = transitionHistory.find(rid);
			if (itT != transitionHistory.end())
				latestTransition = std::max(latestTransition, (int)itT->second);

			auto itP = producerHistory.find(rid);
			if (itP != producerHistory.end())
				latestProducer = std::max(latestProducer, (int)itP->second);
		}
		};

	for (auto const& req : pass.resources.resourceRequirements)
		processResource(req.resourceAndRange.resource.get());

	return { latestTransition, latestProducer };
}

std::pair<int, int> RenderGraph::GetBatchesToWaitOn(
	const RenderPassAndResources& pass,
	std::unordered_map<uint64_t, unsigned int> const& transitionHistory,
	std::unordered_map<uint64_t, unsigned int> const& producerHistory)
{
	int latestTransition = -1, latestProducer = -1;

	auto processResource = [&](Resource* const& res) {
		uint64_t id = res->GetGlobalResourceID();
		for (auto rid : GetAllAliasIDs(id)) {
			auto itT = transitionHistory.find(rid);
			if (itT != transitionHistory.end())
				latestTransition = std::max(latestTransition, (int)itT->second);

			auto itP = producerHistory.find(rid);
			if (itP != producerHistory.end())
				latestProducer = std::max(latestProducer, (int)itP->second);
		}
		};

	for (auto const& req : pass.resources.resourceRequirements)
		processResource(req.resourceAndRange.resource.get());

	return { latestTransition, latestProducer };
}

void RenderGraph::Setup() {
	// Setup the statistics manager
	auto& statisticsManager = StatisticsManager::GetInstance();
	statisticsManager.ClearAll();
	auto& manager = DeviceManager::GetInstance();
	for (auto& batch : batches) {
		for (auto& pass : batch.renderPasses) {
			if (pass.statisticsIndex == -1) {
				pass.statisticsIndex = statisticsManager.RegisterPass(pass.name);
				if (pass.resources.isGeometryPass) {
					statisticsManager.MarkGeometryPass(pass.name);
				}
			}
		}
		for (auto& pass : batch.computePasses) {
			if (pass.statisticsIndex == -1) {
				pass.statisticsIndex = statisticsManager.RegisterPass(pass.name);
			}
		}
	}
	statisticsManager.RegisterQueue(manager.GetGraphicsQueue());
	statisticsManager.RegisterQueue(manager.GetComputeQueue());
	statisticsManager.SetupQueryHeap();

    for (auto& pass : passes) {
        switch (pass.type) {
        case PassType::Render: {
            auto& renderPass = std::get<RenderPassAndResources>(pass.pass);
            renderPass.pass->Setup();
            break;
        }
        case PassType::Compute: {
            auto& computePass = std::get<ComputePassAndResources>(pass.pass);
            computePass.pass->Setup();
            break;
        }
        }
    }
	auto& device = DeviceManager::GetInstance().GetDevice();


    uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
    for (int i = 0; i < numFramesInFlight; i++) {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList7> commandList;
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
        commandList->Close();
        m_graphicsCommandAllocators.push_back(allocator);
        m_graphicsCommandLists.push_back(commandList);

		ComPtr<ID3D12CommandAllocator> computeAllocator;
		ComPtr<ID3D12GraphicsCommandList7> computeCommandList;
		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&computeAllocator)));
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, computeAllocator.Get(), nullptr, IID_PPV_ARGS(&computeCommandList)));
		computeCommandList->Close();
		m_computeCommandAllocators.push_back(computeAllocator);
		m_computeCommandLists.push_back(computeCommandList);
    }
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_graphicsQueueFence));
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeQueueFence));
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameStartSyncFence));

	// Perform initial resource transitions
	//ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&initialTransitionCommandAllocator)));
	//ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, initialTransitionCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&initialTransitionCommandList)));
	//ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_initialTransitionFence)));
	//std::vector<D3D12_BARRIER_GROUP> initialTransitionBarriers;
	//for (auto& transitionPair : initialTransitions) {
	//	auto& transition = transitionPair.second;
	//	auto& group = transition.pResource->GetEnhancedBarrierGroup(transition.prevAccessType, transition.newAccessType, transition.prevLayout, transition.newLayout, transition.prevSyncState, transition.newSyncState);

	//	for (int i = 0; i < group.numBufferBarrierGroups; i++) {
	//		initialTransitionBarriers.push_back(group.bufferBarriers[i]);
	//	}
	//	for (int i = 0; i < group.numTextureBarrierGroups; i++) {
	//		initialTransitionBarriers.push_back(group.textureBarriers[i]);
	//	}
	//	for (int i = 0; i < group.numGlobalBarrierGroups; i++) {
	//		initialTransitionBarriers.push_back(group.globalBarriers[i]);
	//	}
	//}
	//initialTransitionCommandList->Barrier(static_cast<UINT>(initialTransitionBarriers.size()), initialTransitionBarriers.data());
	//initialTransitionCommandList->Close();
	//ID3D12CommandList* pCommandList = initialTransitionCommandList.Get();
	//DeviceManager::GetInstance().GetGraphicsQueue()->ExecuteCommandLists(1, &pCommandList);

 //   // Sync compute and graphics queue
	//DeviceManager::GetInstance().GetGraphicsQueue()->Signal(m_initialTransitionFence.Get(), m_initialTransitionFenceValue);
	//DeviceManager::GetInstance().GetComputeQueue()->Wait(m_initialTransitionFence.Get(), m_initialTransitionFenceValue);
 //   m_initialTransitionFenceValue++;

}

void RenderGraph::AddRenderPass(std::shared_ptr<RenderPass> pass, RenderPassParameters& resources, std::string name) {
    RenderPassAndResources passAndResources;
    passAndResources.pass = pass;
    passAndResources.resources = resources;
	passAndResources.name = name;
	AnyPassAndResources passAndResourcesAny;
	passAndResourcesAny.type = PassType::Render;
	passAndResourcesAny.pass = passAndResources;
	passAndResourcesAny.name = name;
	passes.push_back(passAndResourcesAny);
    if (name != "") {
        renderPassesByName[name] = pass;
    }
}

void RenderGraph::AddComputePass(std::shared_ptr<ComputePass> pass, ComputePassParameters& resources, std::string name) {
	ComputePassAndResources passAndResources;
	passAndResources.pass = pass;
	passAndResources.resources = resources;
	passAndResources.name = name;
	AnyPassAndResources passAndResourcesAny;
	passAndResourcesAny.type = PassType::Compute;
	passAndResourcesAny.pass = passAndResources;
	passAndResourcesAny.name = name;
	passes.push_back(passAndResourcesAny);
	if (name != "") {
		computePassesByName[name] = pass;
	}
}

void RenderGraph::AddResource(std::shared_ptr<Resource> resource, bool transition) {
	auto& name = resource->GetName();

#ifdef _DEBUG
	//if (name == L"") {
	//	throw std::runtime_error("Resource name cannot be empty");
	//}
	//else if (resourcesByName.find(name) != resourcesByName.end()) {
	//	throw std::runtime_error("Resource with name " + ws2s(name) + " already exists");
	//}
#endif

	auto resourceGroup = std::dynamic_pointer_cast<ResourceGroup>(resource);
	if (resourceGroup) {
		resourceGroup->InitializeForGraph();
		resourceGroups.push_back(resourceGroup);
	}

    resourcesByName[name] = resource;
	resourcesByID[resource->GetGlobalResourceID()] = resource;
	trackers[resource->GetGlobalResourceID()] = resource->GetStateTracker();
	/*if (transition) {
		initialResourceStates[resource->GetGlobalResourceID()] = initialState;
	}*/
}

std::shared_ptr<Resource> RenderGraph::GetResourceByName(const std::wstring& name) {
	return resourcesByName[name];
}

std::shared_ptr<Resource> RenderGraph::GetResourceByID(const uint64_t id) {
	return resourcesByID[id];
}
std::shared_ptr<RenderPass> RenderGraph::GetRenderPassByName(const std::string& name) {
    if (renderPassesByName.find(name)!= renderPassesByName.end()) {
        return renderPassesByName[name];
    }
    else {
        return nullptr;
    }
}

std::shared_ptr<ComputePass> RenderGraph::GetComputePassByName(const std::string& name) {
	if (computePassesByName.find(name) != computePassesByName.end()) {
		return computePassesByName[name];
	}
	else {
		return nullptr;
	}
}

void RenderGraph::Update() {
    for (auto& batch : batches) {
        for (auto& passAndResources : batch.renderPasses) {
            passAndResources.pass->Update();
        }
        for (auto& passAndResources : batch.computePasses) {
            passAndResources.pass->Update();
        }
    }
}

#define IFDEBUG(x) 

void RenderGraph::Execute(RenderContext& context) {
	auto& manager = DeviceManager::GetInstance();
	auto graphicsQueue = manager.GetGraphicsQueue();
	auto& graphicsCommandList = m_graphicsCommandLists[context.frameIndex];
	auto& graphicsCommandAllocator = m_graphicsCommandAllocators[context.frameIndex];

	bool useAsyncCompute = false;
	auto computeQueue = graphicsQueue;//manager.GetComputeQueue();
	auto computeCommandList = graphicsCommandList;//m_computeCommandLists[context.frameIndex];
	auto computeCommandAllocator = graphicsCommandAllocator;//m_computeCommandAllocators[context.frameIndex];

	if (useAsyncCompute) {
		computeQueue = manager.GetComputeQueue();
		computeCommandList = m_computeCommandLists[context.frameIndex];
		computeCommandAllocator = m_computeCommandAllocators[context.frameIndex];
	}
    
	UINT64 currentGraphicsQueueFenceOffset = m_graphicsQueueFenceValue*context.frameFenceValue;
	UINT64 currentComputeQueueFenceOffset = m_computeQueueFenceValue*context.frameFenceValue;

    graphicsCommandAllocator->Reset();
	computeCommandAllocator->Reset();

	// Sync compute and graphics queues
    computeQueue->Signal(m_frameStartSyncFence.Get(), context.frameFenceValue);
	graphicsQueue->Wait(m_frameStartSyncFence.Get(), context.frameFenceValue);
    graphicsQueue->Signal(m_frameStartSyncFence.Get(), context.frameFenceValue);
	computeQueue->Wait(m_frameStartSyncFence.Get(), context.frameFenceValue);

	auto& statisticsManager = StatisticsManager::GetInstance();

    for (auto& batch : batches) {

		// Compute queue
		if (batch.computeQueueWaitOnRenderQueueBeforeTransition) {
			computeQueue->Wait(m_graphicsQueueFence.Get(), currentGraphicsQueueFenceOffset+batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue);
		}

		// Perform resource transitions
		std::vector<BarrierGroups> computeBarrierGroups;
		computeCommandList->Reset(computeCommandAllocator.Get(), NULL);
		for (auto& transition : batch.computeTransitions) {
			std::vector<ResourceTransition> dummy;
			transition.pResource->GetStateTracker()->Apply(transition.range, transition.pResource, { transition.newAccessType, transition.newLayout, transition.newSyncState }, dummy);
			auto transitions = transition.pResource->GetEnhancedBarrierGroup(transition.range, transition.prevAccessType, transition.newAccessType, transition.prevLayout, transition.newLayout, transition.prevSyncState, transition.newSyncState);
			computeBarrierGroups.push_back(std::move(transitions));
		}
		std::vector<D3D12_BARRIER_GROUP> computeBarriers;
		for (auto& transition : computeBarrierGroups) {
			computeBarriers.reserve(computeBarriers.size() + transition.bufferBarriers.size() + transition.textureBarriers.size() + transition.globalBarriers.size());
			computeBarriers.insert(computeBarriers.end(), transition.bufferBarriers.begin(), transition.bufferBarriers.end());
			computeBarriers.insert(computeBarriers.end(), transition.textureBarriers.begin(), transition.textureBarriers.end());
			computeBarriers.insert(computeBarriers.end(), transition.globalBarriers.begin(), transition.globalBarriers.end());
		}

		if (computeBarriers.size() > 0) {
			computeCommandList->Barrier(static_cast<UINT>(computeBarriers.size()), computeBarriers.data());
		}
		computeCommandList->Close();
		ID3D12CommandList* ppCommandLists[] = { computeCommandList.Get() };
		computeQueue->ExecuteCommandLists(1, ppCommandLists);
		if (batch.computeTransitionSignal) {
			computeQueue->Signal(m_computeQueueFence.Get(), currentComputeQueueFenceOffset+batch.computeTransitionFenceValue);
		}

		// Wait on render queue if needed
		if (batch.computeQueueWaitOnRenderQueueBeforeExecution) {
			computeQueue->Wait(m_graphicsQueueFence.Get(), currentGraphicsQueueFenceOffset+batch.computeQueueWaitOnRenderQueueBeforeExecutionFenceValue);
		}

		// Execute all passes in the batch
		computeCommandList->Reset(computeCommandAllocator.Get(), NULL);
		context.commandList = computeCommandList.Get();
		std::vector<PassReturn> computeFencesToSignal;
		for (auto& passAndResources : batch.computePasses) {
			if (passAndResources.pass->IsInvalidated()) {
				PIXBeginEvent(computeCommandList.Get(), 0, passAndResources.name.c_str());
				statisticsManager.BeginQuery(passAndResources.statisticsIndex, context.frameIndex, computeQueue, computeCommandList.Get());
				auto passReturn = passAndResources.pass->Execute(context);
				statisticsManager.EndQuery(passAndResources.statisticsIndex, context.frameIndex, computeQueue, computeCommandList.Get());
				PIXEndEvent(computeCommandList.Get());
				if (passReturn.fence != nullptr) {
					computeFencesToSignal.push_back(passReturn);
				}
			}
		}
		statisticsManager.ResolveQueries(context.frameIndex, computeQueue, computeCommandList.Get());
		computeCommandList->Close();

		// Execute commands recorded by compute passes
		ID3D12CommandList* ppComputeBatchesCommandLists[] = { computeCommandList.Get() };
		computeQueue->ExecuteCommandLists(1, ppComputeBatchesCommandLists);

		for (auto& passReturn : computeFencesToSignal) {
			computeQueue->Signal(passReturn.fence, passReturn.fenceValue); // External fences for readback:
		}

		if (batch.computeCompletionSignal) {
			computeQueue->Signal(m_computeQueueFence.Get(), currentComputeQueueFenceOffset+batch.computeCompletionFenceValue);
		}

        // Render queue

		// Wait on compute queue if needed
		if (batch.renderQueueWaitOnComputeQueueBeforeTransition) {
            graphicsQueue->Wait(m_computeQueueFence.Get(), currentComputeQueueFenceOffset+batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue);
		}

        // Perform resource transitions
		//TODO: If a pass is cached, we can skip the transitions, but we may need a new set
        graphicsCommandList->Reset(graphicsCommandAllocator.Get(), NULL);
		std::vector<BarrierGroups> renderBarrierGroups;
        for (auto& transition : batch.renderTransitions) {
			std::vector<ResourceTransition> dummy;
			transition.pResource->GetStateTracker()->Apply(transition.range, transition.pResource, { transition.newAccessType, transition.newLayout, transition.newSyncState }, dummy);
			auto transitions = transition.pResource->GetEnhancedBarrierGroup(transition.range, transition.prevAccessType, transition.newAccessType, transition.prevLayout, transition.newLayout, transition.prevSyncState, transition.newSyncState);
			renderBarrierGroups.push_back(std::move(transitions));
        }
		std::vector<D3D12_BARRIER_GROUP> renderBarriers;
		for (auto& transition : renderBarrierGroups) {
			renderBarriers.reserve(renderBarriers.size() + transition.bufferBarriers.size() + transition.textureBarriers.size() + transition.globalBarriers.size());
			renderBarriers.insert(renderBarriers.end(), transition.bufferBarriers.begin(), transition.bufferBarriers.end());
			renderBarriers.insert(renderBarriers.end(), transition.textureBarriers.begin(), transition.textureBarriers.end());
			renderBarriers.insert(renderBarriers.end(), transition.globalBarriers.begin(), transition.globalBarriers.end());
		}

        if (renderBarriers.size() > 0) {
            graphicsCommandList->Barrier(static_cast<UINT>(renderBarriers.size()), renderBarriers.data());
        }
        graphicsCommandList->Close();
        ID3D12CommandList* ppRenderCommandLists[] = { graphicsCommandList.Get()};
        graphicsQueue->ExecuteCommandLists(1, ppRenderCommandLists);
        if (batch.renderTransitionSignal) {
            graphicsQueue->Signal(m_graphicsQueueFence.Get(), currentGraphicsQueueFenceOffset+batch.renderTransitionFenceValue);
        }


		// Wait on compute queue if needed
		if (batch.renderQueueWaitOnComputeQueueBeforeExecution) {
            graphicsQueue->Wait(m_computeQueueFence.Get(), currentComputeQueueFenceOffset+batch.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue);
		}

        // Execute all passes in the batch
		graphicsCommandList->Reset(graphicsCommandAllocator.Get(), NULL);
		context.commandList = graphicsCommandList.Get();
		std::vector<PassReturn> graphicsFencesToSignal;
        for (auto& passAndResources : batch.renderPasses) {
			if (passAndResources.pass->IsInvalidated()) {
				PIXBeginEvent(graphicsCommandList.Get(), 0, passAndResources.name.c_str());
				statisticsManager.BeginQuery(passAndResources.statisticsIndex, context.frameIndex, graphicsQueue, graphicsCommandList.Get());
                auto passReturn = passAndResources.pass->Execute(context);
				statisticsManager.EndQuery(passAndResources.statisticsIndex, context.frameIndex, graphicsQueue, graphicsCommandList.Get());
				PIXEndEvent(graphicsCommandList.Get());
                if (passReturn.fence != nullptr) {
					graphicsFencesToSignal.push_back(passReturn);
                }
			}
        }
		statisticsManager.ResolveQueries(context.frameIndex, graphicsQueue, graphicsCommandList.Get());
		graphicsCommandList->Close();
		// Execute commands recorded by render passes
		ID3D12CommandList* ppRenderBatchesCommandLists[] = { graphicsCommandList.Get() };
		graphicsQueue->ExecuteCommandLists(1, ppRenderBatchesCommandLists);

		for (auto& passReturn : graphicsFencesToSignal) {
			graphicsQueue->Signal(passReturn.fence, passReturn.fenceValue);
		}

		// Handle special case: Transition resources which will be used on compute queue later, but are in graphic-queue exclusive states
		std::vector<BarrierGroups> passEndTransitions;
		for (auto& transition : batch.passEndTransitions) {
			std::vector<ResourceTransition> dummy;
			transition.pResource->GetStateTracker()->Apply(transition.range, transition.pResource, { transition.newAccessType, transition.newLayout, transition.newSyncState }, dummy);
			auto transitions = transition.pResource->GetEnhancedBarrierGroup(transition.range, transition.prevAccessType, transition.newAccessType, transition.prevLayout, transition.newLayout, transition.prevSyncState, transition.newSyncState);
			passEndTransitions.push_back(std::move(transitions));
		}
		std::vector<D3D12_BARRIER_GROUP> passEndBarriers;
		for (auto& transition : passEndTransitions) {
			passEndBarriers.reserve(passEndBarriers.size() + transition.bufferBarriers.size() + transition.textureBarriers.size() + transition.globalBarriers.size());
			passEndBarriers.insert(passEndBarriers.end(), transition.bufferBarriers.begin(), transition.bufferBarriers.end());
			passEndBarriers.insert(passEndBarriers.end(), transition.textureBarriers.begin(), transition.textureBarriers.end());
			passEndBarriers.insert(passEndBarriers.end(), transition.globalBarriers.begin(), transition.globalBarriers.end());
		}

		graphicsCommandList->Reset(graphicsCommandAllocator.Get(), NULL);
		if (passEndBarriers.size() > 0) {
			graphicsCommandList->Barrier(static_cast<UINT>(passEndBarriers.size()), passEndBarriers.data());
		}
		graphicsCommandList->Close();
		ID3D12CommandList* ppBatchEndCommandLists[] = { graphicsCommandList.Get() };
		graphicsQueue->ExecuteCommandLists(1, ppBatchEndCommandLists);

		// Signal the batch end transition fence
        if (batch.renderCompletionSignal) {
            graphicsQueue->Signal(m_graphicsQueueFence.Get(), currentGraphicsQueueFenceOffset+batch.renderCompletionFenceValue);
        }
    }
}

bool RenderGraph::IsNewBatchNeeded(
	const std::vector<ResourceRequirement>& reqs,
	const std::unordered_map<uint64_t, SymbolicTracker*>& passBatchTrackers,
	const std::unordered_set<uint64_t>& otherQueueUAVs)
{
	// For each subresource requirement in this pass:
	for (auto const &r : reqs) {

		uint64_t id = r.resourceAndRange.resource->GetGlobalResourceID();
		ResourceState wantState{ r.state.access, r.state.layout, r.state.sync };

		// Changing state?
		auto it = passBatchTrackers.find(id);
		if (it != passBatchTrackers.end()) {
			if (it->second->WouldModify(r.resourceAndRange.range, wantState))
				return true;
		}
		// first-use in this batch never forces a split.

		// Cross-queue UAV hazard?
		if ((r.state.access & ResourceAccessType::UNORDERED_ACCESS)
			&& otherQueueUAVs.count(id))
			return true;
		if (r.state.layout == ResourceLayout::LAYOUT_UNORDERED_ACCESS
			&& otherQueueUAVs.count(id))
			return true;
	}
	return false;
}


void RenderGraph::ComputeResourceLoops() {
	PassBatch loopBatch;

	RangeSpec whole{};  

	constexpr ResourceState flushState {
		ResourceAccessType::COMMON,
		ResourceLayout::LAYOUT_COMMON,
		ResourceSyncState::ALL
	};

	for (auto& [id, tracker] : trackers) {
		auto itRes = resourcesByID.find(id);
		if (itRes == resourcesByID.end())
			continue;  // no pointer for this ID? skip

		auto const& pRes = itRes->second;

		tracker->Apply(
			whole, // covers all mips & slices
			pRes.get(),
			flushState,    // the state we’re flushing to
			loopBatch.renderTransitions            // collects all transitions
		);
	}
	batches.push_back(std::move(loopBatch));
}