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
auto RenderGraph::MakeAddTransition(
	std::unordered_map<uint64_t, ResourceAccessType>& finalResourceAccessTypes,
	std::unordered_map<uint64_t, ResourceLayout>& finalResourceLayouts,
	std::unordered_map<uint64_t, ResourceSyncState>& finalResourceSyncStates,
	std::unordered_map<uint64_t, ResourceSyncState>& firstResourceSyncStates,
	std::unordered_map<uint64_t,unsigned int>& transHistCompute,
	std::unordered_map<uint64_t,unsigned int>& transHistRender,
	unsigned int                                   batchIndex,
	PassBatch&                                     currentBatch)
{
	return [&, batchIndex](bool isComputePass, const std::shared_ptr<Resource>& r,
		ResourceAccessType             newAccess,
		ResourceLayout 			  newLayout,
		ResourceSyncState         newSync)
		{
			// Determine old state & sync
			ResourceAccessType prevAccess = r->GetCurrentAccessType();
			ResourceLayout prevLayout = r->GetCurrentLayout();
			ResourceSyncState oldSync  = r->GetPrevSyncState();

			// override if previously recorded
			if (finalResourceAccessTypes.contains(r->GetGlobalResourceID())) {
				prevAccess = finalResourceAccessTypes[r->GetGlobalResourceID()];
			}
			if (finalResourceLayouts.contains(r->GetGlobalResourceID())) {
				prevLayout = finalResourceLayouts[r->GetGlobalResourceID()];
			}
			if (finalResourceSyncStates.contains(r->GetGlobalResourceID())) {
				oldSync = finalResourceSyncStates[r->GetGlobalResourceID()];
			}

			// If the resource is already in the desired state, skip the transition
			// Unless the resource will be accessed as a UAV, then we need a "UAV barrier"
			if (prevAccess == newAccess && !(prevAccess & ResourceAccessType::UNORDERED_ACCESS) &&  prevLayout == newLayout) return;

			ResourceTransition T{r,
				prevAccess, newAccess,
				prevLayout, newLayout,
				oldSync,   newSync};

			// Check if this is a resource group
			std::vector<ResourceTransition> independantlyManagedTransitions;
			auto group = std::dynamic_pointer_cast<ResourceGroup>(r);
			if (group) {
				// If this group has children that are managed directly, we need to explicitly transition them
				for (auto& childID : resourcesFromGroupToManageIndependantly[group->GetGlobalResourceID()]) {
					auto& child = resourcesByID[childID];
					if (child) {
						//if (initialTransitions.contains(r->GetGlobalResourceID())) {
							independantlyManagedTransitions.push_back(ResourceTransition{ child, prevAccess, newAccess, prevLayout, newLayout, oldSync, newSync });
						//}
						//else {
						//	initialTransitions[child->GetGlobalResourceID()] = ResourceTransition{ child, prevAccess, newAccess, prevLayout, newLayout, oldSync, newSync };
						//}
					} else {
						spdlog::error("Resource group {} has a child resource {} that is marked as independantly managed, but is not managed by this graph. This should not happen.", group->GetGlobalResourceID(), childID);
						throw(std::runtime_error("Resource group has a child resource that is not managed by this graph"));
					}
				}
			}

			bool oldSyncIsNotComputeSyncState = ResourceSyncStateIsNotComputeSyncState(oldSync);
			if (isComputePass && oldSyncIsNotComputeSyncState) {
				// bounce back to last graphics-queue batch
				unsigned int gfxBatch = transHistRender[r->GetGlobalResourceID()];
				//if (initialTransitions.contains(r->GetGlobalResourceID())) {
					batches[gfxBatch].passEndTransitions.push_back(T);
				//}
				//else {
				//	initialTransitions[r->GetGlobalResourceID()] = T;
				//}
				transHistRender[r->GetGlobalResourceID()] = gfxBatch;
				for (auto& transition : independantlyManagedTransitions) {
					//if (initialTransitions.contains(r->GetGlobalResourceID())) {
						batches[gfxBatch].passEndTransitions.push_back(transition);
					//}
					//else {
					//	initialTransitions[r->GetGlobalResourceID()] = transition;
					//}
					transHistRender[transition.pResource->GetGlobalResourceID()] = gfxBatch;
					finalResourceAccessTypes[transition.pResource->GetGlobalResourceID()] = transition.newAccessType;
					finalResourceLayouts[transition.pResource->GetGlobalResourceID()] = transition.newLayout;
					finalResourceSyncStates[transition.pResource->GetGlobalResourceID()] = transition.newSyncState;
				}
			}
			else {
				if (isComputePass) {
					//if (initialTransitions.contains(r->GetGlobalResourceID())) {
						currentBatch.computeTransitions.push_back(T);
					//}
					//else {
					//	initialTransitions[r->GetGlobalResourceID()] = T;
					//}
					transHistCompute[r->GetGlobalResourceID()] = batchIndex;
					for (auto& transition : independantlyManagedTransitions) {
						//if (initialTransitions.contains(r->GetGlobalResourceID())) {
							currentBatch.computeTransitions.push_back(transition);
						//}
						//else {
						//	initialTransitions[transition.pResource->GetGlobalResourceID()] = transition;
						//}
						transHistCompute[transition.pResource->GetGlobalResourceID()] = batchIndex;
						finalResourceAccessTypes[transition.pResource->GetGlobalResourceID()] = transition.newAccessType;
						finalResourceLayouts[transition.pResource->GetGlobalResourceID()] = transition.newLayout;
						finalResourceSyncStates[transition.pResource->GetGlobalResourceID()] = transition.newSyncState;
					}
				} else {
					//if (initialTransitions.contains(r->GetGlobalResourceID())) {
						currentBatch.renderTransitions.push_back(T);
					//}
					//else {
					//	initialTransitions[r->GetGlobalResourceID()] = T;
					//}
					transHistRender[r->GetGlobalResourceID()]  = batchIndex;
					for (auto& transition : independantlyManagedTransitions) {
						//if (initialTransitions.contains(r->GetGlobalResourceID())) {
							currentBatch.renderTransitions.push_back(transition);
						//}
						//else {
						//	initialTransitions[transition.pResource->GetGlobalResourceID()] = transition;
						//}
						transHistRender[transition.pResource->GetGlobalResourceID()] = batchIndex;
						finalResourceAccessTypes[transition.pResource->GetGlobalResourceID()] = transition.newAccessType;
						finalResourceLayouts[transition.pResource->GetGlobalResourceID()] = transition.newLayout;
						finalResourceSyncStates[transition.pResource->GetGlobalResourceID()] = transition.newSyncState;
					}
				}
			}
		};
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

    auto currentBatch = PassBatch();
    currentBatch.renderTransitionFenceValue = GetNextGraphicsQueueFenceValue();
    currentBatch.renderCompletionFenceValue = GetNextGraphicsQueueFenceValue();
    currentBatch.computeTransitionFenceValue = GetNextComputeQueueFenceValue();
    currentBatch.computeCompletionFenceValue = GetNextComputeQueueFenceValue();
    //std::unordered_map<std::wstring, ResourceState> previousBatchResourceStates;
    std::unordered_map<uint64_t, ResourceAccessType> finalResourceAccessTypes;
	std::unordered_map<uint64_t, ResourceLayout> finalResourceLayouts;
	std::unordered_map<uint64_t, ResourceSyncState> finalResourceSyncStates;

	std::unordered_map<uint64_t, ResourceAccessType> firstResourceAccessTypes;
	std::unordered_map<uint64_t, ResourceLayout> firstResourceLayouts;
	std::unordered_map<uint64_t, ResourceSyncState> firstResourceSyncStates;

	std::unordered_set<uint64_t> computeUAVs;
	std::unordered_set<uint64_t> renderUAVs;

    std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueTransition;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueTransition;

	std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueProducer;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueProducer;

	unsigned int currentBatchIndex = 0;
    for (auto& pr : passes) {


		bool isCompute = (pr.type == PassType::Compute);
		if (isCompute) {
			auto& pass = std::get<ComputePassAndResources>(pr.pass);
			if (IsNewBatchNeeded(currentBatch, pass, renderUAVs)) {
				for (auto& [id, st] : currentBatch.resourceAccessTypes)
					finalResourceAccessTypes[id] = st;
				for (auto& [id, st] : currentBatch.resourceLayouts)
					finalResourceLayouts[id] = st;
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
			if (IsNewBatchNeeded(currentBatch, pass, computeUAVs)) {
				for (auto& [id, st] : currentBatch.resourceAccessTypes)
					finalResourceAccessTypes[id] = st;
				for (auto& [id, st] : currentBatch.resourceLayouts)
					finalResourceLayouts[id] = st;
				batches.push_back(std::move(currentBatch));
				currentBatch = PassBatch();
				currentBatch.renderTransitionFenceValue   = GetNextGraphicsQueueFenceValue();
				currentBatch.renderCompletionFenceValue   = GetNextGraphicsQueueFenceValue();
				currentBatch.computeTransitionFenceValue  = GetNextComputeQueueFenceValue();
				currentBatch.computeCompletionFenceValue  = GetNextComputeQueueFenceValue();
				++currentBatchIndex;
			}
		}

		// build the transition lambda
		auto addT = MakeAddTransition(
			finalResourceAccessTypes,
			finalResourceLayouts,
			finalResourceSyncStates,
			firstResourceSyncStates,
			batchOfLastComputeQueueTransition,
			batchOfLastRenderQueueTransition,
			currentBatchIndex,
			currentBatch
		);

		// dispatch categories
		if (isCompute) {
			auto& pass = std::get<ComputePassAndResources>(pr.pass);
			ProcessResourceRequirements(
				isCompute,
				pass.resources.resourceRequirements,
				addT,
				finalResourceAccessTypes,
				finalResourceLayouts,
				finalResourceSyncStates,
				firstResourceAccessTypes,
				firstResourceLayouts,
				firstResourceSyncStates,
				batchOfLastComputeQueueTransition,
				currentBatchIndex);
			currentBatch.computePasses.push_back(pass);
			UpdateDesiredResourceStates(currentBatch, pass, computeUAVs);
		} else {
			auto& pass = std::get<RenderPassAndResources>(pr.pass);
			ProcessResourceRequirements(
				isCompute,
				pass.resources.resourceRequirements,
				addT,
				finalResourceAccessTypes,
				finalResourceLayouts,
				finalResourceSyncStates,
				firstResourceAccessTypes,
				firstResourceLayouts,
				firstResourceSyncStates,
				batchOfLastRenderQueueTransition,
				currentBatchIndex);
			currentBatch.renderPasses.push_back(pass);
			UpdateDesiredResourceStates(currentBatch, pass, renderUAVs);
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
    for (const auto& [resourceID, accessType] : currentBatch.resourceAccessTypes) {
        finalResourceAccessTypes[resourceID] = accessType;
    }
	for (const auto& [resourceID, layout] : currentBatch.resourceLayouts) {
		finalResourceLayouts[resourceID] = layout;
	}
    batches.push_back(std::move(currentBatch));

    // Insert transitions to loop resources back to their initial states
	ComputeResourceLoops(
		finalResourceAccessTypes,
		finalResourceLayouts,
		finalResourceSyncStates,
		firstResourceAccessTypes,
		firstResourceLayouts,
		firstResourceSyncStates
		);

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

std::pair<int, int> RenderGraph::GetBatchesToWaitOn(const ComputePassAndResources& pass, const std::unordered_map<uint64_t, unsigned int>& transitionHistory, const std::unordered_map<uint64_t, unsigned int>& producerHistory) {
	int latestTransition = -1;
	int latestProducer = -1;

    auto processResource = [&](const std::shared_ptr<Resource>& resource) {
        if (transitionHistory.contains(resource->GetGlobalResourceID())) {
            latestTransition = (std::max)(latestTransition, (int)transitionHistory.at(resource->GetGlobalResourceID()));
        }
        if (producerHistory.contains(resource->GetGlobalResourceID())) {
            latestProducer = (std::max)(latestProducer, (int)producerHistory.at(resource->GetGlobalResourceID()));
        }
        };

	for (const ResourceRequirement& resourceRequirement : pass.resources.resourceRequirements) {
		processResource(resourceRequirement.resource);
	}

	return { latestTransition, latestProducer };
}

std::pair<int, int> RenderGraph::GetBatchesToWaitOn(const RenderPassAndResources& pass, const std::unordered_map<uint64_t, unsigned int>& transitionHistory, const std::unordered_map<uint64_t, unsigned int>& producerHistory) {
	int latestTransition = -1;
	int latestProducer = -1;

	auto processResource = [&](const std::shared_ptr<Resource>& resource) {
		if (transitionHistory.contains(resource->GetGlobalResourceID())) {
			latestTransition = (std::max)(latestTransition, (int)transitionHistory.at(resource->GetGlobalResourceID()));
		}
		if (producerHistory.contains(resource->GetGlobalResourceID())) {
			latestProducer = (std::max)(latestProducer, (int)producerHistory.at(resource->GetGlobalResourceID()));
		}
		};

	for (const ResourceRequirement& resourceRequirement : pass.resources.resourceRequirements) {
		processResource(resourceRequirement.resource);
	}

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

	CreateBatchCommandLists();
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
	auto computeQueue = manager.GetComputeQueue();
	auto& graphicsCommandList = m_graphicsCommandLists[context.frameIndex];
	auto& graphicsCommandAllocator = m_graphicsCommandAllocators[context.frameIndex];
	auto& computeCommandList = m_computeCommandLists[context.frameIndex];
	auto& computeCommandAllocator = m_computeCommandAllocators[context.frameIndex];

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
		computeCommandList->Reset(computeCommandAllocator.Get(), NULL);
		std::vector<D3D12_BARRIER_GROUP> computeBarriers;
		for (auto& transition : batch.computeTransitions) {
			auto& transitions = transition.pResource->GetEnhancedBarrierGroup(transition.prevAccessType, transition.newAccessType, transition.prevLayout, transition.newLayout, transition.prevSyncState, transition.newSyncState);
			computeBarriers.reserve(computeBarriers.size() + transitions.numBufferBarrierGroups + transitions.numTextureBarrierGroups + transitions.numGlobalBarrierGroups);
			computeBarriers.insert(computeBarriers.end(), transitions.bufferBarriers, transitions.bufferBarriers + transitions.numBufferBarrierGroups);
			computeBarriers.insert(computeBarriers.end(), transitions.textureBarriers, transitions.textureBarriers + transitions.numTextureBarrierGroups);
			computeBarriers.insert(computeBarriers.end(), transitions.globalBarriers, transitions.globalBarriers + transitions.numGlobalBarrierGroups);
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
				DEBUG_ONLY(PIXBeginEvent(computeCommandList.Get(), 0, passAndResources.name.c_str()));
				statisticsManager.BeginQuery(passAndResources.statisticsIndex, context.frameIndex, computeQueue, computeCommandList.Get());
				auto passReturn = passAndResources.pass->Execute(context);
				statisticsManager.EndQuery(passAndResources.statisticsIndex, context.frameIndex, computeQueue, computeCommandList.Get());
				DEBUG_ONLY(PIXEndEvent(computeCommandList.Get()));
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
		std::vector<D3D12_BARRIER_GROUP> renderBarriers;
        for (auto& transition : batch.renderTransitions) {
            auto& transitions = transition.pResource->GetEnhancedBarrierGroup(transition.prevAccessType, transition.newAccessType, transition.prevLayout, transition.newLayout, transition.prevSyncState, transition.newSyncState);
			renderBarriers.reserve(renderBarriers.size() + transitions.numBufferBarrierGroups + transitions.numTextureBarrierGroups + transitions.numGlobalBarrierGroups);
			renderBarriers.insert(renderBarriers.end(), transitions.bufferBarriers, transitions.bufferBarriers + transitions.numBufferBarrierGroups);
			renderBarriers.insert(renderBarriers.end(), transitions.textureBarriers, transitions.textureBarriers + transitions.numTextureBarrierGroups);
			renderBarriers.insert(renderBarriers.end(), transitions.globalBarriers, transitions.globalBarriers + transitions.numGlobalBarrierGroups);
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
				DEBUG_ONLY(PIXBeginEvent(graphicsCommandList.Get(), 0, passAndResources.name.c_str()));
				statisticsManager.BeginQuery(passAndResources.statisticsIndex, context.frameIndex, graphicsQueue, graphicsCommandList.Get());
                auto passReturn = passAndResources.pass->Execute(context);
				statisticsManager.EndQuery(passAndResources.statisticsIndex, context.frameIndex, graphicsQueue, graphicsCommandList.Get());
				DEBUG_ONLY(PIXEndEvent(graphicsCommandList.Get()));
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
		std::vector<D3D12_BARRIER_GROUP> passEndBarriers;
		for (auto& transition : batch.passEndTransitions) {
			auto& transitions = transition.pResource->GetEnhancedBarrierGroup(transition.prevAccessType, transition.newAccessType, transition.prevLayout, transition.newLayout, transition.prevSyncState, transition.newSyncState);
			passEndBarriers.reserve(passEndBarriers.size() + transitions.numBufferBarrierGroups + transitions.numTextureBarrierGroups + transitions.numGlobalBarrierGroups);
			passEndBarriers.insert(passEndBarriers.end(), transitions.bufferBarriers, transitions.bufferBarriers + transitions.numBufferBarrierGroups);
			passEndBarriers.insert(passEndBarriers.end(), transitions.textureBarriers, transitions.textureBarriers + transitions.numTextureBarrierGroups);
			passEndBarriers.insert(passEndBarriers.end(), transitions.globalBarriers, transitions.globalBarriers + transitions.numGlobalBarrierGroups);
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

bool RenderGraph::IsNewBatchNeeded(PassBatch& currentBatch, const RenderPassAndResources& passAndResources, const std::unordered_set<uint64_t>& computeUAVs) {
	//return true;
	
	// New batch is needed if (a) current batch has a resource we need for this pass in a different state
	// Or (b) if this pass would use a resource in a manner that would cause a cross-queue read/write hazard
	for (auto& requirement : passAndResources.resources.resourceRequirements) {
		auto resourceID = requirement.resource->GetGlobalResourceID();
		if (currentBatch.resourceAccessTypes.contains(resourceID) && currentBatch.resourceAccessTypes[resourceID] != requirement.access) {
			return true;
		}
		if (currentBatch.resourceLayouts.contains(resourceID) && currentBatch.resourceLayouts[resourceID] != requirement.layout) {
			return true;
		}
		if (requirement.access & ResourceAccessType::UNORDERED_ACCESS && computeUAVs.contains(resourceID)) {
			return true;
		}
		if (requirement.layout == ResourceLayout::LAYOUT_UNORDERED_ACCESS && ResourceLayoutIsUnorderedAccess(currentBatch.resourceLayouts[resourceID])) {
			return true;
		}

	}
	return false;
}

bool RenderGraph::IsNewBatchNeeded(PassBatch& currentBatch, const ComputePassAndResources& passAndResources, const std::unordered_set<uint64_t>& renderUAVs) {
	//return true;

	// New batch is needed if (a) current batch has a resource we need for this pass in a different state
    // Or (b) if this pass would use a resource in a manner that would cause a cross-queue read/write hazard
	for (auto& requirement : passAndResources.resources.resourceRequirements) {
		auto resourceID = requirement.resource->GetGlobalResourceID();
		if (currentBatch.resourceAccessTypes.contains(resourceID) && currentBatch.resourceAccessTypes[resourceID] != requirement.access) {
			return true;
		}
		if (currentBatch.resourceLayouts.contains(resourceID) && currentBatch.resourceLayouts[resourceID] != requirement.layout) {
			return true;
		}
		if (requirement.access & ResourceAccessType::UNORDERED_ACCESS && renderUAVs.contains(resourceID)) {
			return true;
		}
		if (requirement.layout == ResourceLayout::LAYOUT_UNORDERED_ACCESS && ResourceLayoutIsUnorderedAccess(currentBatch.resourceLayouts[resourceID])) {
			return true;
		}
	}
    return false;
}

void RenderGraph::UpdateDesiredResourceStates(PassBatch& batch, RenderPassAndResources& passAndResources, std::unordered_set<uint64_t>& renderUAVs) {
	for (auto& requirement : passAndResources.resources.resourceRequirements) {
		batch.resourceAccessTypes[requirement.resource->GetGlobalResourceID()] = requirement.access;
		batch.resourceLayouts[requirement.resource->GetGlobalResourceID()] = requirement.layout;
	}
}

void RenderGraph::UpdateDesiredResourceStates(PassBatch& batch, ComputePassAndResources& passAndResources, std::unordered_set<uint64_t>& computeUAVs) {
	for (auto& requirement : passAndResources.resources.resourceRequirements) {
		batch.resourceAccessTypes[requirement.resource->GetGlobalResourceID()] = requirement.access;
		batch.resourceLayouts[requirement.resource->GetGlobalResourceID()] = requirement.layout;
	}
}


void RenderGraph::ComputeResourceLoops(
	std::unordered_map<uint64_t, ResourceAccessType>& finalResourceAccessTypes,
	std::unordered_map<uint64_t, ResourceLayout>& finalResourceLayouts,
	std::unordered_map<uint64_t, ResourceSyncState>& finalResourceSyncStates, 
	std::unordered_map<uint64_t, ResourceAccessType>& firstAccessTypes,
	std::unordered_map<uint64_t, ResourceLayout>& firstLayouts,
	std::unordered_map<uint64_t, ResourceSyncState>& firstResourceSyncStates) {
	PassBatch loopBatch;
    for (const auto& [resourceID, finalAccessType] : finalResourceAccessTypes) {
        auto resource = GetResourceByID(resourceID);
        if (!resource) {
            throw std::runtime_error(ws2s(L"Resource not found: " + resourceID));
        }

		auto finalLayout = finalResourceLayouts[resourceID];
		auto finalSyncState = finalResourceSyncStates[resourceID];

		auto firstAccessType = firstAccessTypes[resourceID];
		auto firstLayout = firstLayouts[resourceID];
		auto firstSyncState = firstResourceSyncStates[resourceID];

        //if (finalLayout != firstLayout || finalAccessType != firstAccessType) {
            // Insert a transition to bring the resource back to its initial state
            ResourceTransition transition = { resource, finalAccessType, ResourceAccessType::COMMON, finalLayout, ResourceLayout::LAYOUT_COMMON, finalSyncState, ResourceSyncState::ALL};
            loopBatch.renderTransitions.push_back(transition);
        //}
    }
	batches.push_back(std::move(loopBatch));
}

RenderPassBuilder RenderGraph::BuildRenderPass(std::string name) {
	return RenderPassBuilder(*this, name);
}
ComputePassBuilder RenderGraph::BuildComputePass(std::string name) {
	return ComputePassBuilder(*this, name);
}

void RenderGraph::CreateBatchCommandLists() {
	// For each batch, create numFramesInFlight command allocators and lists
	//auto& device = DeviceManager::GetInstance().GetDevice();
	//auto numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
	//for (auto& batch : batches) {
	//	for (int i = 0; i < numFramesInFlight; i++) {
	//		if (batch.computePasses.size() > 0) {
	//			ComPtr<ID3D12GraphicsCommandList7> commandList;
	//			ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeCommandAllocators[i].Get(), nullptr, IID_PPV_ARGS(&commandList)));
	//			commandList->Close();
	//			batch.computeCommandLists.push_back(commandList);
	//		}
	//		if (batch.renderPasses.size() > 0) {
	//			ComPtr<ID3D12GraphicsCommandList7> commandList;
	//			ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_graphicsCommandAllocators[i].Get(), nullptr, IID_PPV_ARGS(&commandList)));
	//			commandList->Close();
	//			batch.renderCommandLists.push_back(commandList);
	//		}
	//	}
	//}

}