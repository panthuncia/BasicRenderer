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
#include "Managers/CommandRecordingManager.h"

// Factory for the transition lambda
void RenderGraph::AddTransition(
	std::unordered_map<uint64_t, unsigned int>&  batchOfLastRenderQueueUsage,
	unsigned int batchIndex,
	PassBatch& currentBatch,
	bool isComputePass, 
	const ResourceRequirement& r,
	std::unordered_set<uint64_t>& outTransitionedResourceIDs)
{

	auto& resource = r.resourceAndRange.resource;

	std::vector<ResourceTransition> transitions;
	resource->GetStateTracker()->Apply(r.resourceAndRange.range, resource.get(), r.state, transitions);

	if (!transitions.empty()) {
		outTransitionedResourceIDs.insert(resource->GetGlobalResourceID());
	}

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
		unsigned int gfxBatch = batchOfLastRenderQueueUsage[resource->GetGlobalResourceID()];
		for (auto& transition : transitions) {
			batchOfLastRenderQueueUsage[transition.pResource->GetGlobalResourceID()] = gfxBatch; // Can this cause transition overlaps?
			batches[gfxBatch].passEndTransitions.push_back(transition);
		}
	}
	else {
		if (isComputePass) {
			for (auto& transition : transitions) {
				//context.transHistCompute[transition.pResource->GetGlobalResourceID()] = batchIndex;
				currentBatch.computeTransitions.push_back(transition);
			}
		}
		else {
			for (auto& transition : transitions) {
				//context.transHistRender[transition.pResource->GetGlobalResourceID()] = batchIndex;
				currentBatch.renderTransitions.push_back(transition);
			}
		}
	}
}

void RenderGraph::ProcessResourceRequirements(
	bool isCompute,
	std::vector<ResourceRequirement>& resourceRequirements,
	std::unordered_map<uint64_t, unsigned int>& batchOfLastRenderQueueUsage,
	std::unordered_map<uint64_t, unsigned int>& producerHistory,
	unsigned int batchIndex,
	PassBatch& currentBatch, std::unordered_set<uint64_t>& outTransitionedResourceIDs) {

	for (auto& resourceRequirement : resourceRequirements) {

		if (!resourcesByID.contains(resourceRequirement.resourceAndRange.resource->GetGlobalResourceID())) {
			spdlog::error("Resource referenced by pass is not managed by this graph");
			throw(std::runtime_error("Resource referenced is not managed by this graph"));
		}

		const auto& id = resourceRequirement.resourceAndRange.resource->GetGlobalResourceID();

		AddTransition(batchOfLastRenderQueueUsage, batchIndex, currentBatch, isCompute, resourceRequirement, outTransitionedResourceIDs);

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
	// Register resource providers from pass builders


	for (auto& v : m_passBuilders) {
		std::visit([this](auto& builder) {
			RegisterProvider(builder.pass.get());
			}, v);
	}

	for (auto& v : m_passBuilders) {
		std::visit([](auto& builder) {
			builder.Finalize();
			}, v);
	}

    batches.clear();

	//Check if any of the resource groups we have have Nth children that we also manage directly
	for (auto& resourceGroup : resourceGroups) {
		auto children = resourceGroup->GetChildIDs();
		for (auto& childID : children) {
			if (resourcesByID.contains(childID)) {
				resourceGroup->MarkResourceAsNonStandard(resourcesByID[childID]);
				if (!resourcesFromGroupToManageIndependantly.contains(resourceGroup->GetGlobalResourceID())) {
					resourcesFromGroupToManageIndependantly[resourceGroup->GetGlobalResourceID()] = {};
				}
				resourcesFromGroupToManageIndependantly[resourceGroup->GetGlobalResourceID()].push_back(childID);
				independantlyManagedResourceToGroup[childID] = resourceGroup->GetGlobalResourceID();
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

	std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueUsage;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueUsage;

	unsigned int currentBatchIndex = 0;
    for (auto& pr : passes) {

		bool isCompute = (pr.type == PassType::Compute);

		if (isCompute) {
			auto& pass = std::get<ComputePassAndResources>(pr.pass);
			if (IsNewBatchNeeded(pass.resources.resourceRequirements, 
				pass.resources.internalTransitions, 
				currentBatch.passBatchTrackers, 
				currentBatch.internallyTransitionedResources, 
				currentBatch.allResources,
				renderUAVs)) {
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
			if (IsNewBatchNeeded(pass.resources.resourceRequirements,
				pass.resources.internalTransitions,
				currentBatch.passBatchTrackers,
				currentBatch.internallyTransitionedResources,
				currentBatch.allResources,
				computeUAVs)) {
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
		std::unordered_set<uint64_t> resourcesTransitionedThisPass;
		// dispatch categories
		if (isCompute) {
			auto& pass = std::get<ComputePassAndResources>(pr.pass);
			ProcessResourceRequirements(
				isCompute,
				pass.resources.resourceRequirements,
				batchOfLastRenderQueueUsage,
				batchOfLastComputeQueueTransition,
				currentBatchIndex,
				currentBatch,
				resourcesTransitionedThisPass);
			currentBatch.computePasses.push_back(pass);
			for (auto& exit : pass.resources.internalTransitions) { // If this pass transitions internally, update to the exit state
				std::vector<ResourceTransition> _; // Ignored
				exit.first.resource->GetStateTracker()->Apply(exit.first.range, exit.first.resource.get(), exit.second, _);
				currentBatch.internallyTransitionedResources.insert(exit.first.resource->GetGlobalResourceID());
			}
			for (auto& req : pass.resources.resourceRequirements) {
				currentBatch.allResources.insert(req.resourceAndRange.resource->GetGlobalResourceID());
				batchOfLastComputeQueueUsage[req.resourceAndRange.resource->GetGlobalResourceID()] = currentBatchIndex;
			}
		} else {
			auto& pass = std::get<RenderPassAndResources>(pr.pass);
			ProcessResourceRequirements(
				isCompute,
				pass.resources.resourceRequirements,
				batchOfLastRenderQueueUsage,
				batchOfLastRenderQueueTransition,
				currentBatchIndex,
				currentBatch,
				resourcesTransitionedThisPass);
			currentBatch.renderPasses.push_back(pass);
			for (auto& exit : pass.resources.internalTransitions) {
				std::vector<ResourceTransition> _;
				exit.first.resource->GetStateTracker()->Apply(exit.first.range, exit.first.resource.get(), exit.second, _);
				currentBatch.internallyTransitionedResources.insert(exit.first.resource->GetGlobalResourceID());
			}
			for (auto& req : pass.resources.resourceRequirements) {
				if (req.resourceAndRange.resource->GetGlobalResourceID() == 2) {
					spdlog::info("2");
				}
				currentBatch.allResources.insert(req.resourceAndRange.resource->GetGlobalResourceID());
				batchOfLastRenderQueueUsage[req.resourceAndRange.resource->GetGlobalResourceID()] = currentBatchIndex;
			}
		}

		if (isCompute) {
			applySynchronization(
				/*isComputePass=*/true,
				currentBatch,
				currentBatchIndex,
				std::get<ComputePassAndResources>(pr.pass),
				batchOfLastRenderQueueTransition,
				batchOfLastRenderQueueProducer,
				batchOfLastRenderQueueUsage,
				resourcesTransitionedThisPass
			);
		} else {
			applySynchronization(
				/*isComputePass=*/false,
				currentBatch,
				currentBatchIndex,
				std::get<RenderPassAndResources>(pr.pass),
				batchOfLastComputeQueueTransition,
				batchOfLastComputeQueueProducer,
				batchOfLastComputeQueueUsage,
				resourcesTransitionedThisPass
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

std::tuple<int, int, int> RenderGraph::GetBatchesToWaitOn(
	const ComputePassAndResources& pass,
	std::unordered_map<uint64_t, unsigned int> const& transitionHistory,
	std::unordered_map<uint64_t, unsigned int> const& producerHistory,
	std::unordered_map<uint64_t, unsigned int> const& usageHistory,
	std::unordered_set<uint64_t> const& resourcesTransitionedThisPass)
{
	int latestTransition = -1, latestProducer = -1, latestUsage = -1;

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

	for (auto& transitionID : resourcesTransitionedThisPass) { // We only need to wait on the latest usage for resources that will be transitioned in this batch
		for (auto rid : GetAllAliasIDs(transitionID)) {
			if (usageHistory.contains(rid)) {
				latestUsage = std::max(latestUsage, (int)usageHistory.at(rid));
			}
		}
	}

	return { latestTransition, latestProducer, latestUsage };
}

std::tuple<int, int, int> RenderGraph::GetBatchesToWaitOn(
	const RenderPassAndResources& pass,
	std::unordered_map<uint64_t, unsigned int> const& transitionHistory,
	std::unordered_map<uint64_t, unsigned int> const& producerHistory,
	std::unordered_map<uint64_t, unsigned int> const& usageHistory,
	std::unordered_set<uint64_t> const& resourcesTransitionedThisPass)
{
	int latestTransition = -1, latestProducer = -1, latestUsage = -1;

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

	for (auto& transitionID : resourcesTransitionedThisPass) { // We only need to wait on the latest usage for resources that will be transitioned in this batch
		for (auto rid : GetAllAliasIDs(transitionID)) {
			if (usageHistory.contains(rid)) {
				latestUsage = std::max(latestTransition, (int)usageHistory.at(rid));
			}
		}
	}

	return { latestTransition, latestProducer, latestUsage };
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

	auto& device = DeviceManager::GetInstance().GetDevice();

	m_graphicsCommandListPool = std::make_unique<CommandListPool>(device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_computeCommandListPool = std::make_unique<CommandListPool>(device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_copyCommandListPool = std::make_unique<CommandListPool>(device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);

	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_graphicsQueueFence));
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeQueueFence));
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyQueueFence));
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameStartSyncFence));

	bool useAsyncCompute = true;

	CommandRecordingManager::Init init{
		.graphicsQ = manager.GetGraphicsQueue(),
		.graphicsF = m_graphicsQueueFence.Get(),
		.graphicsPool = m_graphicsCommandListPool.get(),

		.computeQ = useAsyncCompute ? manager.GetComputeQueue() : manager.GetGraphicsQueue(),
		.computeF = m_computeQueueFence.Get(),
		.computePool = useAsyncCompute ? m_computeCommandListPool.get() : m_graphicsCommandListPool.get(),

		.copyQ = manager.GetCopyQueue(),
		.copyF = m_copyQueueFence.Get(),
		.copyPool = m_copyCommandListPool.get(),
		.computeMode = useAsyncCompute ? ComputeMode::Async : ComputeMode::AliasToGraphics
	};
	m_pCommandRecordingManager = std::make_unique<CommandRecordingManager>(init);

	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> emptyLists;
	for (auto& pass : passes) {
		switch (pass.type) {
		case PassType::Render: {
			auto& renderPass = std::get<RenderPassAndResources>(pass.pass);
			renderPass.pass->SetResourceRegistryView(std::make_unique<ResourceRegistryView>(_registry, renderPass.resources.identifierSet));
			renderPass.pass->Setup();
			renderPass.pass->RegisterCommandLists(emptyLists);
			break;
		}
		case PassType::Compute: {
			auto& computePass = std::get<ComputePassAndResources>(pass.pass);
			computePass.pass->SetResourceRegistryView(std::make_unique<ResourceRegistryView>(_registry, computePass.resources.identifierSet));
			computePass.pass->Setup();
			computePass.pass->RegisterCommandLists(emptyLists);
			break;
		}
		}
	}
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

namespace {
	void ExecuteTransitions(std::vector<ResourceTransition>& transitions,
		CommandRecordingManager* crm,
		QueueKind queueKind,
		ID3D12GraphicsCommandList10* commandList,
		UINT64 fenceOffset,
		bool fenceSignal,
		UINT64 fenceValue) {
		std::vector<BarrierGroups> groups;
		for (auto& transition : transitions) {
			std::vector<ResourceTransition> dummy;
			transition.pResource->GetStateTracker()->Apply(
				transition.range, transition.pResource,
				{ transition.newAccessType, transition.newLayout, transition.newSyncState }, dummy);
			auto bg = transition.pResource->GetEnhancedBarrierGroup(
				transition.range, transition.prevAccessType, transition.newAccessType,
				transition.prevLayout, transition.newLayout,
				transition.prevSyncState, transition.newSyncState);
			groups.push_back(std::move(bg));
		}
		std::vector<D3D12_BARRIER_GROUP> barriers;
		for (auto& g : groups) {
			barriers.reserve(barriers.size() + g.bufferBarriers.size() +
				g.textureBarriers.size() + g.globalBarriers.size());
			barriers.insert(barriers.end(), g.bufferBarriers.begin(), g.bufferBarriers.end());
			barriers.insert(barriers.end(), g.textureBarriers.begin(), g.textureBarriers.end());
			barriers.insert(barriers.end(), g.globalBarriers.begin(), g.globalBarriers.end());
		}
		if (!barriers.empty()) {
			commandList->Barrier(static_cast<UINT>(barriers.size()), barriers.data());
		}
		if (fenceSignal) {
			UINT64 signalValue = fenceOffset + fenceValue;
			crm->Flush(queueKind, { true, signalValue });
		}
	}

	template<typename PassT>
	void ExecutePasses(std::vector<PassT>& passes,
		CommandRecordingManager* crm,
		ID3D12CommandQueue* queue,
		QueueKind queueKind,
		ID3D12GraphicsCommandList10* commandList,
		UINT64 fenceOffset,
		bool fenceSignal,
		UINT64 fenceValue,
		RenderContext& context,
		StatisticsManager& statisticsManager) {
		std::vector<PassReturn> externalFences;
		context.commandList = commandList;
		for (auto& pr : passes) {
			if (pr.pass->IsInvalidated()) {
				PIXBeginEvent(commandList, 0, pr.name.c_str());
				statisticsManager.BeginQuery(pr.statisticsIndex, context.frameIndex, queue, commandList);
				auto passReturn = pr.pass->Execute(context);
				statisticsManager.EndQuery(pr.statisticsIndex, context.frameIndex, queue, commandList);
				PIXEndEvent(commandList);
				if (passReturn.fence != nullptr) {
					externalFences.push_back(passReturn);
				}
			}
		}
		statisticsManager.ResolveQueries(context.frameIndex, queue, commandList);
		if (externalFences.size() > 0 || fenceSignal) {
			UINT64 signalValue = fenceOffset + fenceValue;
			crm->Flush(queueKind, { fenceSignal, signalValue });
			for (auto& fr : externalFences) {
				queue->Signal(fr.fence, fr.fenceValue);
			}
		}
	}
} // namespace

void RenderGraph::Execute(RenderContext& context) {
	auto crm = m_pCommandRecordingManager.get();

	auto graphicsQueue = crm->Queue(QueueKind::Graphics);
	auto computeQueue = crm->Queue(QueueKind::Compute);

	const bool alias = (computeQueue == graphicsQueue);
	auto WaitIfDistinct = [&](ID3D12CommandQueue* dstQ, ID3D12Fence* fence, UINT64 val) {
		if (!alias) dstQ->Wait(fence, val);
		};

	UINT64 currentGraphicsQueueFenceOffset = m_graphicsQueueFenceValue * context.frameFenceValue;
	UINT64 currentComputeQueueFenceOffset = m_computeQueueFenceValue * context.frameFenceValue;

	computeQueue->Signal(m_frameStartSyncFence.Get(), context.frameFenceValue);
	graphicsQueue->Wait(m_frameStartSyncFence.Get(), context.frameFenceValue);
	graphicsQueue->Signal(m_frameStartSyncFence.Get(), context.frameFenceValue);
	computeQueue->Wait(m_frameStartSyncFence.Get(), context.frameFenceValue);

	auto& statisticsManager = StatisticsManager::GetInstance();

	unsigned int batchIndex = 0;
	for (auto& batch : batches) {

		if (batch.computeQueueWaitOnRenderQueueBeforeTransition) {
			WaitIfDistinct(computeQueue, m_graphicsQueueFence.Get(),
				currentGraphicsQueueFenceOffset +
				batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue);
		}

		auto computeCommandList = crm->EnsureOpen(QueueKind::Compute, context.frameIndex); // TODO: Frame index or frame #?

		ExecuteTransitions(batch.computeTransitions,
			crm,
			QueueKind::Compute,
			computeCommandList,
			currentComputeQueueFenceOffset, 
			batch.computeTransitionSignal,
			batch.computeTransitionFenceValue);

		if (batch.computeQueueWaitOnRenderQueueBeforeExecution) {
			WaitIfDistinct(computeQueue, m_graphicsQueueFence.Get(),
				currentGraphicsQueueFenceOffset +
				batch.computeQueueWaitOnRenderQueueBeforeExecutionFenceValue);
		}

		ExecutePasses(batch.computePasses, 
			crm,
			computeQueue, 
			QueueKind::Compute,
			computeCommandList,
			currentComputeQueueFenceOffset,
			batch.computeCompletionSignal, 
			batch.computeCompletionFenceValue,
			context, 
			statisticsManager);

		if (batch.renderQueueWaitOnComputeQueueBeforeTransition) {
			WaitIfDistinct(graphicsQueue, m_computeQueueFence.Get(),
				currentComputeQueueFenceOffset +
				batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue);
		}

		auto graphicsCommandList = crm->EnsureOpen(QueueKind::Graphics, context.frameIndex);

		ExecuteTransitions(batch.renderTransitions,
			crm,
			QueueKind::Graphics,
			graphicsCommandList,
			currentGraphicsQueueFenceOffset, 
			batch.renderTransitionSignal,
			batch.renderTransitionFenceValue);

		if (batch.renderQueueWaitOnComputeQueueBeforeExecution) {
			WaitIfDistinct(graphicsQueue, m_computeQueueFence.Get(),
				currentComputeQueueFenceOffset +
				batch.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue);
		}

		bool signalNow = batch.passEndTransitions.size() == 0 && batch.renderCompletionSignal ? true : false;

		ExecutePasses(batch.renderPasses, 
			crm,
			graphicsQueue, 
			QueueKind::Graphics,
			graphicsCommandList,
			currentGraphicsQueueFenceOffset, 
			signalNow,
			batch.renderCompletionFenceValue,
			context, 
			statisticsManager);

		if (batch.passEndTransitions.size() > 0) {
			ExecuteTransitions(batch.passEndTransitions, 
				crm,
				QueueKind::Graphics,
				graphicsCommandList,
				currentGraphicsQueueFenceOffset,
				batch.renderCompletionSignal, 
				batch.renderCompletionFenceValue);
		}
		++batchIndex;
	}
	crm->Flush(QueueKind::Graphics, { false, 0 });
	crm->Flush(QueueKind::Compute, { false, 0 });
	crm->EndFrame();
}

bool RenderGraph::IsNewBatchNeeded(
	const std::vector<ResourceRequirement>& reqs,
	const std::vector<std::pair<ResourceAndRange, ResourceState>> passInternalTransitions,
	const std::unordered_map<uint64_t, SymbolicTracker*>& passBatchTrackers,
	const std::unordered_set<uint64_t>& currentBatchInternallyTransitionedResources,
	const std::unordered_set<uint64_t>& currentBatchAllResources,
	const std::unordered_set<uint64_t>& otherQueueUAVs)
{
	// For each internally modified resource
	for (auto const& r : passInternalTransitions) {
		auto id = r.first.resource->GetGlobalResourceID();
		// If this resource is used in the current batch, we need a new one
		if (currentBatchAllResources.contains(id)) {
			return true;
		}
		// If this resource is part of a resource group, and this batch uses that group, we need a new batch
		if (independantlyManagedResourceToGroup.contains(id) && currentBatchAllResources.contains(independantlyManagedResourceToGroup[id])) {
			return true;
		}
	}

	// For each subresource requirement in this pass:
	for (auto const &r : reqs) {

		uint64_t id = r.resourceAndRange.resource->GetGlobalResourceID();

		// If this resource is internally modified in the current batch, we need a new one
		if (currentBatchInternallyTransitionedResources.count(id)) {
			return true;
		}

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
			flushState,    // the state were flushing to
			loopBatch.renderTransitions            // collects all transitions
		);
	}
	batches.push_back(std::move(loopBatch));
}

void RenderGraph::RegisterProvider(IResourceProvider* prov) {
	auto keys = prov->GetSupportedKeys();
	for (const auto& key : keys) {
		if (_providerMap.find(key) != _providerMap.end()) {
			std::string_view name = key.ToString();
			throw std::runtime_error("Resource provider already registered for key: " + std::string(name));
		}
		_providerMap[key] = prov;
	}
	_providers.push_back(prov);

	// Register all resources provided by this provider
	for (const auto& key : prov->GetSupportedKeys()) {
		auto resource = prov->ProvideResource(key);
		if (resource) {
			RegisterResource(key, resource, prov);
		} else {
			spdlog::warn("Provider returned null for advertised key: {}", key.ToString());
		}
	}
}

void RenderGraph::RegisterResource(ResourceIdentifier id, std::shared_ptr<Resource> resource,
	IResourceProvider* provider) {
	_registry.Register(id, resource);
	AddResource(resource);
	if (provider) {
		_providerMap[id] = provider;
	}
}

std::shared_ptr<Resource> RenderGraph::RequestResource(ResourceIdentifier const& rid, bool allowFailure) {
	// If it's already in our registry, return it
	auto cached = _registry.Request(rid);
	if (cached) {
		return cached;
	}

	// We don't have it in our registry, check if we have a provider for it
	auto providerIt = _providerMap.find(rid);
	if (providerIt != _providerMap.end()) {
		// If we have a provider for this key, use it to provide the resource
		auto provider = providerIt->second;
		if (provider) {
			auto resource = provider->ProvideResource(rid);
			if (resource) {
				// Register the resource in our registry
				_registry.Register(rid, resource);
				AddResource(resource);
				return resource;
			}
			else {
				throw std::runtime_error("Provider returned null for key: " + rid.ToString());
			}
		}
	}

	// No provider registered for this key
	if (allowFailure) {
		// If we are allowed to fail, return nullptr
		return nullptr;
	}
	throw std::runtime_error("No resource provider registered for key: " + rid.ToString());
}

ComputePassBuilder RenderGraph::BuildComputePass(std::string const& name) {
	return ComputePassBuilder(this, name);
}
RenderPassBuilder RenderGraph::BuildRenderPass(std::string const& name) {
	return RenderPassBuilder(this, name);
}

void RenderGraph::RegisterPassBuilder(RenderPassBuilder&& builder) {
	m_passBuilders.emplace_back(std::move(builder));
}
void RenderGraph::RegisterPassBuilder(ComputePassBuilder&& builder) {
	m_passBuilders.emplace_back(std::move(builder));
}