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

static bool mapHasResourceNotInState(std::unordered_map<uint64_t, ResourceState>& map, uint64_t resourceID, ResourceState state) {
    return mapHasKeyNotAsValue<uint64_t, ResourceState>(map, resourceID, state);
}

static bool mapHasResourceInUAVState(std::unordered_map<uint64_t, ResourceState>& map, uint64_t resourceID) {
	bool a = mapHasKeyAsValue<uint64_t, ResourceState>(map, resourceID, ResourceState::ALL_SRV);
}

// Factory for the transition lambda
auto RenderGraph::MakeAddTransition(
	std::unordered_map<uint64_t, ResourceState>& finalResourceStates,
	std::unordered_map<uint64_t, ResourceSyncState>& finalResourceSyncStates,
	std::unordered_map<uint64_t, ResourceSyncState>& firstResourceSyncStates,
	std::unordered_map<uint64_t,unsigned int>& transHistCompute,
	std::unordered_map<uint64_t,unsigned int>& transHistRender,
	unsigned int                                   batchIndex,
	PassBatch&                                     currentBatch)
{
	return [&, batchIndex](bool isComputePass, const std::shared_ptr<Resource>& r,
		ResourceState             newState,
		ResourceSyncState         newSync)
		{
			// Determine old state & sync
			ResourceState     oldState = r->GetState();
			ResourceSyncState oldSync  = ResourceSyncState::ALL;

			// override if previously recorded
			if (initialResourceStates.contains(r->GetGlobalResourceID())) { // Start with first state we recieved this resource in
				oldState = initialResourceStates[r->GetGlobalResourceID()];
			}
			if (finalResourceStates.contains(r->GetGlobalResourceID())) { // If we've modified this resource in a previous pass
				oldState = finalResourceStates[r->GetGlobalResourceID()];
			}
			if (finalResourceSyncStates.contains(r->GetGlobalResourceID())) {
				oldSync = finalResourceSyncStates[r->GetGlobalResourceID()];
			}

			auto prevAccess = ResourceStateToAccessType(oldState);
			auto newAccess  = ResourceStateToAccessType(newState);
			if (prevAccess == newAccess && oldSync == newSync) return;

			ResourceTransition T{r, oldState, newState,
				prevAccess, newAccess,
				oldSync,   newSync};

			// Check if this is a resource group
			std::vector<ResourceTransition> independantlyManagedTransitions;
			auto group = std::dynamic_pointer_cast<ResourceGroup>(r);
			if (group) {
				// If this group has children that are managed directly, we need to explicitly transition them
				for (auto& childID : resourcesFromGroupToManageIndependantly[group->GetGlobalResourceID()]) {
					auto& child = resourcesByID[childID];
					if (child) {
						independantlyManagedTransitions.push_back(ResourceTransition{ child, oldState, newState, prevAccess, newAccess, oldSync, newSync });
					} else {
						spdlog::error("Resource group {} has a child resource {} that is marked as independantly managed, but is not managed by this graph. This should not happen.", group->GetGlobalResourceID(), childID);
						throw(std::runtime_error("Resource group has a child resource that is not managed by this graph"));
					}
				}
			}

			bool oldSyncIsNotComputeSyncState = ResourceSyncStateIsNotComputeSyncState(oldSync);
			if (isComputePass && oldSyncIsNotComputeSyncState && !(oldState == ResourceState::UNKNOWN)) {
				// bounce back to last graphics-queue batch
				unsigned int gfxBatch = transHistRender[r->GetGlobalResourceID()];
				batches[gfxBatch].passEndTransitions.push_back(T);
				transHistRender[r->GetGlobalResourceID()] = gfxBatch;
				for (auto& transition : independantlyManagedTransitions) {
					batches[gfxBatch].passEndTransitions.push_back(transition);
					transHistRender[transition.pResource->GetGlobalResourceID()] = gfxBatch;
				}
			}
			else {
				if (isComputePass) {
					currentBatch.computeTransitions.push_back(T);
					transHistCompute[r->GetGlobalResourceID()] = batchIndex;
					for (auto& transition : independantlyManagedTransitions) {
						currentBatch.computeTransitions.push_back(transition);
						transHistCompute[transition.pResource->GetGlobalResourceID()] = batchIndex;
					}
				} else {
					currentBatch.renderTransitions.push_back(T);
					transHistRender[r->GetGlobalResourceID()]  = batchIndex;
					for (auto& transition : independantlyManagedTransitions) {
						currentBatch.renderTransitions.push_back(transition);
						transHistRender[transition.pResource->GetGlobalResourceID()] = batchIndex;
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
    std::unordered_map<uint64_t, ResourceState> finalResourceStates;
	std::unordered_map<uint64_t, ResourceSyncState> finalResourceSyncStates;
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

		// Batch-splitting logic (unchanged)
		if (isCompute) {
			auto& pass = std::get<ComputePassAndResources>(pr.pass);
			if (IsNewBatchNeeded(currentBatch, pass, renderUAVs)) {
				for (auto& [id, st] : currentBatch.resourceStates)
					finalResourceStates[id] = st;
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
				for (auto& [id, st] : currentBatch.resourceStates)
					finalResourceStates[id] = st;
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
			finalResourceStates,
			finalResourceSyncStates,
			firstResourceSyncStates,
			batchOfLastComputeQueueTransition,
			batchOfLastRenderQueueTransition,
			currentBatchIndex,
			currentBatch
		);

		// choose categories
		std::vector<ResourceCategory> catsCompute = {};
		std::vector<ResourceCategory> catsRender = {};

		if (isCompute) {
			catsCompute = {
				{ &std::get<ComputePassAndResources>(pr.pass).resources.shaderResources, ResourceState::NON_PIXEL_SRV, ResourceSyncState::COMPUTE_SHADING, false },
				{ &std::get<ComputePassAndResources>(pr.pass).resources.constantBuffers, ResourceState::CONSTANT,       ResourceSyncState::COMPUTE_SHADING, false },
				{ &std::get<ComputePassAndResources>(pr.pass).resources.unorderedAccessViews, ResourceState::UNORDERED_ACCESS, ResourceSyncState::COMPUTE_SHADING, true }
			};
		} else {
			catsRender = {
				{ &std::get<RenderPassAndResources>(pr.pass).resources.shaderResources,   ResourceState::ALL_SRV,      ResourceSyncState::DRAW, false },
				{ &std::get<RenderPassAndResources>(pr.pass).resources.renderTargets,     ResourceState::RENDER_TARGET,ResourceSyncState::DRAW, true  },
				{ &std::get<RenderPassAndResources>(pr.pass).resources.depthTextures,     ResourceState::DEPTH_WRITE,  ResourceSyncState::DRAW, true  },
				{ &std::get<RenderPassAndResources>(pr.pass).resources.constantBuffers,   ResourceState::CONSTANT,     ResourceSyncState::DRAW, false },
				{ &std::get<RenderPassAndResources>(pr.pass).resources.unorderedAccessViews, ResourceState::UNORDERED_ACCESS, ResourceSyncState::DRAW, true },
				{ &std::get<RenderPassAndResources>(pr.pass).resources.copyTargets,      ResourceState::COPY_DEST,   ResourceSyncState::COPY, true  },
				{ &std::get<RenderPassAndResources>(pr.pass).resources.copySources,      ResourceState::COPY_SOURCE, ResourceSyncState::COPY, false },
				{ &std::get<RenderPassAndResources>(pr.pass).resources.indirectArgumentBuffers, ResourceState::INDIRECT_ARGUMENT, ResourceSyncState::DRAW, false }
			};
		}

		// dispatch categories
		if (isCompute) {
			ProcessCategories(
				isCompute,
				std::span(catsCompute),
				addT,
				finalResourceStates,
				finalResourceSyncStates,
				firstResourceSyncStates,
				batchOfLastComputeQueueProducer,
				currentBatchIndex);
			currentBatch.computePasses.push_back(std::get<ComputePassAndResources>(pr.pass));
			UpdateDesiredResourceStates(currentBatch, std::get<ComputePassAndResources>(pr.pass), computeUAVs);
		} else {
			ProcessCategories(
				isCompute,
				std::span(catsRender),
				addT,
				finalResourceStates,
				finalResourceSyncStates,
				firstResourceSyncStates,
				batchOfLastRenderQueueProducer,
				currentBatchIndex);
			currentBatch.renderPasses.push_back(std::get<RenderPassAndResources>(pr.pass));
			UpdateDesiredResourceStates(currentBatch, std::get<RenderPassAndResources>(pr.pass), renderUAVs);
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
    for (const auto& [resourceName, state] : currentBatch.resourceStates) {
        finalResourceStates[resourceName] = state;
    }
    batches.push_back(std::move(currentBatch));

    // Insert transitions to loop resources back to their initial states
    ComputeResourceLoops(finalResourceStates, finalResourceSyncStates, firstResourceSyncStates);

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

	for (const auto& resource : pass.resources.unorderedAccessViews) {
		processResource(resource);
	}
	for (const auto& resource : pass.resources.constantBuffers) {
		processResource(resource);
	}
	for (const auto& resource : pass.resources.shaderResources) {
		processResource(resource);
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

    for (const auto& resource : pass.resources.unorderedAccessViews) {
        processResource(resource);
    }
    for (const auto& resource : pass.resources.constantBuffers) {
        processResource(resource);
    }
    for (const auto& resource : pass.resources.shaderResources) {
        processResource(resource);
    }
	for (const auto& resource : pass.resources.renderTargets) {
		processResource(resource);
	}
	for (const auto& resource : pass.resources.depthTextures) {
		processResource(resource);
	}
	for (const auto& resource : pass.resources.copyTargets) {
		processResource(resource);
	}
	for (const auto& resource : pass.resources.copySources) {
		processResource(resource);
	}
	for (const auto& resource : pass.resources.indirectArgumentBuffers) {
		processResource(resource);
	}

    return { latestTransition, latestProducer };
}

void RenderGraph::Setup() {
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
        m_graphicsTransitionCommandLists.push_back(commandList);

		ComPtr<ID3D12GraphicsCommandList7> batchEndCommandList;
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&batchEndCommandList)));
		batchEndCommandList->Close();
		m_graphicsBatchEndTransitionCommandLists.push_back(batchEndCommandList);

		ComPtr<ID3D12CommandAllocator> computeAllocator;
		ComPtr<ID3D12GraphicsCommandList7> computeCommandList;
		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&computeAllocator)));
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, computeAllocator.Get(), nullptr, IID_PPV_ARGS(&computeCommandList)));
		computeCommandList->Close();
		m_computeCommandAllocators.push_back(computeAllocator);
		m_computeTransitionCommandLists.push_back(computeCommandList);
    }
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_graphicsQueueFence));
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeQueueFence));
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameStartSyncFence));


	// Perform initial resource transitions
	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&initialTransitionCommandAllocator)));
	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, initialTransitionCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&initialTransitionCommandList)));
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_initialTransitionFence)));
    for (auto& resourcePair : initialResourceStates) {
		auto& resource = resourcesByID[resourcePair.first];
		auto& state = resourcePair.second;
		auto currentState = resource->GetState();
		if (currentState == state) {
			continue;
		}
		auto transitions = resource->GetTransitions(currentState, state);
		for (auto& transition : transitions) {
			initialTransitionCommandList->ResourceBarrier(1, &transition);
		}
    }
	initialTransitionCommandList->Close();
	ID3D12CommandList* pCommandList = initialTransitionCommandList.Get();
	DeviceManager::GetInstance().GetGraphicsQueue()->ExecuteCommandLists(1, &pCommandList);

    // Sync compute and graphics queue
	DeviceManager::GetInstance().GetGraphicsQueue()->Signal(m_initialTransitionFence.Get(), m_initialTransitionFenceValue);
	DeviceManager::GetInstance().GetComputeQueue()->Wait(m_initialTransitionFence.Get(), m_initialTransitionFenceValue);
    m_initialTransitionFenceValue++;
}

void RenderGraph::AddRenderPass(std::shared_ptr<RenderPass> pass, RenderPassParameters& resources, std::string name) {
    RenderPassAndResources passAndResources;
    passAndResources.pass = pass;
    passAndResources.resources = resources;
	AnyPassAndResources passAndResourcesAny;
	passAndResourcesAny.type = PassType::Render;
	passAndResourcesAny.pass = passAndResources;
	passes.push_back(passAndResourcesAny);
    if (name != "") {
        renderPassesByName[name] = pass;
    }
}

void RenderGraph::AddComputePass(std::shared_ptr<ComputePass> pass, ComputePassParameters& resources, std::string name) {
	ComputePassAndResources passAndResources;
	passAndResources.pass = pass;
	passAndResources.resources = resources;
	AnyPassAndResources passAndResourcesAny;
	passAndResourcesAny.type = PassType::Compute;
	passAndResourcesAny.pass = passAndResources;
	passes.push_back(passAndResourcesAny);
	if (name != "") {
		computePassesByName[name] = pass;
	}
}

void RenderGraph::AddResource(std::shared_ptr<Resource> resource, bool transition, ResourceState initialState) {
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
	if (transition) {
		initialResourceStates[resource->GetGlobalResourceID()] = initialState;
	}
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

void RenderGraph::Execute(RenderContext& context) {
	auto& manager = DeviceManager::GetInstance();
	auto graphicsQueue = manager.GetGraphicsQueue();
	auto computeQueue = manager.GetComputeQueue();
	auto& graphicsTransitionCommandList = m_graphicsTransitionCommandLists[context.frameIndex];
	auto& graphicsBatchEndTransitionCommandList = m_graphicsBatchEndTransitionCommandLists[context.frameIndex];
	auto& graphicsCommandAllocator = m_graphicsCommandAllocators[context.frameIndex];
	auto& computeTransitionCommandList = m_computeTransitionCommandLists[context.frameIndex];
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

    for (auto& batch : batches) {

		// Compute queue
		if (batch.computeQueueWaitOnRenderQueueBeforeTransition) {
			computeQueue->Wait(m_graphicsQueueFence.Get(), currentGraphicsQueueFenceOffset+batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue);
		}

		// Perform resource transitions
		computeTransitionCommandList->Reset(computeCommandAllocator.Get(), NULL);
		std::vector<D3D12_BARRIER_GROUP> computeBarriers;
		for (auto& transition : batch.computeTransitions) {
			auto& transitions = transition.pResource->GetEnhancedBarrierGroup(transition.fromState, transition.toState, transition.prevAccessType, transition.newAccessType, transition.prevSyncState, transition.newSyncState);
			computeBarriers.reserve(computeBarriers.size() + transitions.numBufferBarrierGroups + transitions.numTextureBarrierGroups + transitions.numGlobalBarrierGroups);
			computeBarriers.insert(computeBarriers.end(), transitions.bufferBarriers, transitions.bufferBarriers + transitions.numBufferBarrierGroups);
			computeBarriers.insert(computeBarriers.end(), transitions.textureBarriers, transitions.textureBarriers + transitions.numTextureBarrierGroups);
			computeBarriers.insert(computeBarriers.end(), transitions.globalBarriers, transitions.globalBarriers + transitions.numGlobalBarrierGroups);
		}
		if (computeBarriers.size() > 0) {
			computeTransitionCommandList->Barrier(static_cast<UINT>(computeBarriers.size()), computeBarriers.data());
		}
		computeTransitionCommandList->Close();
		ID3D12CommandList* ppCommandLists[] = { computeTransitionCommandList.Get() };
		computeQueue->ExecuteCommandLists(1, ppCommandLists);
		if (batch.computeTransitionSignal) {
			computeQueue->Signal(m_computeQueueFence.Get(), currentComputeQueueFenceOffset+batch.computeTransitionFenceValue);
		}

		// Wait on render queue if needed
		if (batch.computeQueueWaitOnRenderQueueBeforeExecution) {
			computeQueue->Wait(m_graphicsQueueFence.Get(), currentGraphicsQueueFenceOffset+batch.computeQueueWaitOnRenderQueueBeforeExecutionFenceValue);
		}

		// Execute all passes in the batch
		for (auto& passAndResources : batch.computePasses) {
			if (passAndResources.pass->IsInvalidated()) {
				auto passReturn = passAndResources.pass->Execute(context);
				ID3D12CommandList** ppComputeCommandLists = reinterpret_cast<ID3D12CommandList**>(passReturn.commandLists.data());
				computeQueue->ExecuteCommandLists(static_cast<UINT>(passReturn.commandLists.size()), ppComputeCommandLists);
                
				if (passReturn.fence != nullptr) {
					computeQueue->Signal(passReturn.fence, passReturn.fenceValue); // External fences for readback: TODO merge with new fence system
				}
			}
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
        graphicsTransitionCommandList->Reset(graphicsCommandAllocator.Get(), NULL);
		std::vector<D3D12_BARRIER_GROUP> renderBarriers;
        for (auto& transition : batch.renderTransitions) {
            auto& transitions = transition.pResource->GetEnhancedBarrierGroup(transition.fromState, transition.toState, transition.prevAccessType, transition.newAccessType, transition.prevSyncState, transition.newSyncState);
			renderBarriers.reserve(renderBarriers.size() + transitions.numBufferBarrierGroups + transitions.numTextureBarrierGroups + transitions.numGlobalBarrierGroups);
			renderBarriers.insert(renderBarriers.end(), transitions.bufferBarriers, transitions.bufferBarriers + transitions.numBufferBarrierGroups);
			renderBarriers.insert(renderBarriers.end(), transitions.textureBarriers, transitions.textureBarriers + transitions.numTextureBarrierGroups);
			renderBarriers.insert(renderBarriers.end(), transitions.globalBarriers, transitions.globalBarriers + transitions.numGlobalBarrierGroups);
        }
        if (renderBarriers.size() > 0) {
            graphicsTransitionCommandList->Barrier(static_cast<UINT>(renderBarriers.size()), renderBarriers.data());
        }
        graphicsTransitionCommandList->Close();
        ID3D12CommandList* ppRenderCommandLists[] = { graphicsTransitionCommandList.Get()};
        graphicsQueue->ExecuteCommandLists(1, ppRenderCommandLists);
        if (batch.renderTransitionSignal) {
            graphicsQueue->Signal(m_graphicsQueueFence.Get(), currentGraphicsQueueFenceOffset+batch.renderTransitionFenceValue);
        }


		// Wait on compute queue if needed
		if (batch.renderQueueWaitOnComputeQueueBeforeExecution) {
            graphicsQueue->Wait(m_computeQueueFence.Get(), currentComputeQueueFenceOffset+batch.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue);
		}

        // Execute all passes in the batch
        for (auto& passAndResources : batch.renderPasses) {
			if (passAndResources.pass->IsInvalidated()) {

                auto passReturn = passAndResources.pass->Execute(context);
				ID3D12CommandList** ppCommandLists = reinterpret_cast<ID3D12CommandList**>(passReturn.commandLists.data());
                graphicsQueue->ExecuteCommandLists(static_cast<UINT>(passReturn.commandLists.size()), ppCommandLists);

                if (passReturn.fence != nullptr) {
                    graphicsQueue->Signal(passReturn.fence, passReturn.fenceValue); // External fences for readback: TODO merge with new fence system
                }
			}
        }

		// Handle special case: Transition resources which will be used on compute queue later, but are in graphic-queue exclusive states
		std::vector<D3D12_BARRIER_GROUP> passEndBarriers;
		for (auto& transition : batch.passEndTransitions) {
			auto& transitions = transition.pResource->GetEnhancedBarrierGroup(transition.fromState, transition.toState, transition.prevAccessType, transition.newAccessType, transition.prevSyncState, transition.newSyncState);
			passEndBarriers.reserve(passEndBarriers.size() + transitions.numBufferBarrierGroups + transitions.numTextureBarrierGroups + transitions.numGlobalBarrierGroups);
			passEndBarriers.insert(passEndBarriers.end(), transitions.bufferBarriers, transitions.bufferBarriers + transitions.numBufferBarrierGroups);
			passEndBarriers.insert(passEndBarriers.end(), transitions.textureBarriers, transitions.textureBarriers + transitions.numTextureBarrierGroups);
			passEndBarriers.insert(passEndBarriers.end(), transitions.globalBarriers, transitions.globalBarriers + transitions.numGlobalBarrierGroups);
		}

		graphicsBatchEndTransitionCommandList->Reset(graphicsCommandAllocator.Get(), NULL);
		if (passEndBarriers.size() > 0) {
			graphicsBatchEndTransitionCommandList->Barrier(static_cast<UINT>(passEndBarriers.size()), passEndBarriers.data());
		}
		graphicsBatchEndTransitionCommandList->Close();
		ID3D12CommandList* ppBatchEndCommandLists[] = { graphicsBatchEndTransitionCommandList.Get() };
		graphicsQueue->ExecuteCommandLists(1, ppBatchEndCommandLists);

		// Signal the batch end transition fence
        if (batch.renderCompletionSignal) {
            graphicsQueue->Signal(m_graphicsQueueFence.Get(), currentGraphicsQueueFenceOffset+batch.renderCompletionFenceValue);
        }
    }
}

bool RenderGraph::IsNewBatchNeeded(PassBatch& currentBatch, const RenderPassAndResources& passAndResources, const std::unordered_set<uint64_t>& computeUAVs) {
	// New batch is needed if (a) current batch has a resource we need for this pass in a different state
	// Or (b) if this pass would use a resource in a manner that would cause a cross-queue read/write hazard
    for (auto& resource : passAndResources.resources.shaderResources) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::ALL_SRV)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.renderTargets) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::RENDER_TARGET)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.depthTextures) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::DEPTH_WRITE)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::CONSTANT)) {
            return true;
        }
    }
	for (auto& resource : passAndResources.resources.unorderedAccessViews) {
		if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::UNORDERED_ACCESS)) {
			return true;
		}
		if (computeUAVs.contains(resource->GetGlobalResourceID())) {
			return true;
		}
	}
	for (auto& resource : passAndResources.resources.copySources) {
		if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::COPY_SOURCE)) {
			return true;
		}
	}
    for (auto& resource : passAndResources.resources.copyTargets) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::COPY_DEST)) {
            return true;
        }
    }
	for (auto& resource : passAndResources.resources.indirectArgumentBuffers) {
		if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::INDIRECT_ARGUMENT)) {
			return true;
		}
	}
    return false;
}

bool RenderGraph::IsNewBatchNeeded(PassBatch& currentBatch, const ComputePassAndResources& passAndResources, const std::unordered_set<uint64_t>& renderUAVs) {
    // New batch is needed if (a) current batch has a resource we need for this pass in a different state
    // Or (b) if this pass would use a resource in a manner that would cause a cross-queue read/write hazard
    for (auto& resource : passAndResources.resources.shaderResources) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::NON_PIXEL_SRV)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::CONSTANT)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.unorderedAccessViews) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetGlobalResourceID(), ResourceState::UNORDERED_ACCESS)) {
            return true;
        }
		if (renderUAVs.contains(resource->GetGlobalResourceID())) {
			return true;
		}
    }
    return false;
}

void RenderGraph::UpdateDesiredResourceStates(PassBatch& batch, RenderPassAndResources& passAndResources, std::unordered_set<uint64_t>& renderUAVs) {
    // Update batch.resourceStates based on the resources used in passAndResources
    for (auto& resource : passAndResources.resources.shaderResources) {
        batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::ALL_SRV;
    }
    for (auto& resource : passAndResources.resources.renderTargets) {
        batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::RENDER_TARGET;
    }
    for (auto& resource : passAndResources.resources.depthTextures) {
        batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::DEPTH_WRITE;
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::CONSTANT;
    }
    for (auto& resource : passAndResources.resources.unorderedAccessViews) {
		batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::UNORDERED_ACCESS;
		renderUAVs.insert(resource->GetGlobalResourceID());
    }
	for (auto& resource : passAndResources.resources.copySources) {
		batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::COPY_SOURCE;
	}
	for (auto& resource : passAndResources.resources.copyTargets) {
		batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::COPY_DEST;
	}
	for (auto& resource : passAndResources.resources.indirectArgumentBuffers) {
		batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::INDIRECT_ARGUMENT;
	}
}

void RenderGraph::UpdateDesiredResourceStates(PassBatch& batch, ComputePassAndResources& passAndResources, std::unordered_set<uint64_t>& computeUAVs) {
    // Update batch.resourceStates based on the resources used in passAndResources
    for (auto& resource : passAndResources.resources.shaderResources) {
        batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::NON_PIXEL_SRV;
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::CONSTANT;
    }
    for (auto& resource : passAndResources.resources.unorderedAccessViews) {
        batch.resourceStates[resource->GetGlobalResourceID()] = ResourceState::UNORDERED_ACCESS;
        computeUAVs.insert(resource->GetGlobalResourceID());
    }
}


void RenderGraph::ComputeResourceLoops(
	const std::unordered_map<uint64_t, ResourceState>& finalResourceStates, 
	std::unordered_map<uint64_t, ResourceSyncState>& finalResourceSyncStates, 
	std::unordered_map<uint64_t, ResourceSyncState>& firstResourceSyncStates) {
	PassBatch loopBatch;
    for (const auto& [resourceID, finalState] : finalResourceStates) {
        auto resource = GetResourceByID(resourceID);
        if (!resource) {
            throw std::runtime_error(ws2s(L"Resource not found: " + resourceID));
        }

        ResourceState initialState = resource->GetState();

		auto finalSyncState = finalResourceSyncStates[resourceID];
		auto firstResourceSyncState = firstResourceSyncStates[resourceID];

        if (finalState != initialState || firstResourceSyncState != finalSyncState) {
            // Insert a transition to bring the resource back to its initial state
			ResourceAccessType firstAccessType = ResourceStateToAccessType(initialState);
			ResourceAccessType newAccessType = ResourceStateToAccessType(finalState);
            ResourceTransition transition = { resource, finalState, initialState, newAccessType, firstAccessType, finalSyncState, firstResourceSyncState};
            loopBatch.renderTransitions.push_back(transition);
        }
    }
	batches.push_back(std::move(loopBatch));
}

RenderPassBuilder RenderGraph::BuildRenderPass(std::string name) {
	return RenderPassBuilder(*this, name);
}
ComputePassBuilder RenderGraph::BuildComputePass(std::string name) {
	return ComputePassBuilder(*this, name);
}