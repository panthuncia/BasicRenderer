#include "Render/RenderGraph.h"

#include "Render/RenderContext.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/ReadbackManager.h"
#include "Managers/Singletons/DeviceManager.h"

static bool mapHasResourceNotInState(std::unordered_map<std::wstring, ResourceState>& map, std::wstring resourceName, ResourceState state) {
    return mapHasKeyNotAsValue<std::wstring, ResourceState>(map, resourceName, state);
}

static bool mapHasResourceInUAVState(std::unordered_map<std::wstring, ResourceState>& map, std::wstring resourceName) {
	bool a = mapHasKeyAsValue<std::wstring, ResourceState>(map, resourceName, ResourceState::ALL_SRV);
}

void RenderGraph::Compile() {
    batches.clear();
    auto currentBatch = PassBatch();
    currentBatch.renderTransitionFenceValue = GetNextGraphicsQueueFenceValue();
    currentBatch.renderCompletionFenceValue = GetNextGraphicsQueueFenceValue();
    currentBatch.computeTransitionFenceValue = GetNextComputeQueueFenceValue();
    currentBatch.computeCompletionFenceValue = GetNextComputeQueueFenceValue();
    //std::unordered_map<std::wstring, ResourceState> previousBatchResourceStates;
    std::unordered_map<std::wstring, ResourceState> finalResourceStates;
	std::unordered_map<std::wstring, ResourceSyncState> finalResourceSyncStates;

	std::unordered_set<std::wstring> computeUAVs;
	std::unordered_set<std::wstring> renderUAVs;

    std::unordered_map<std::wstring, unsigned int>  batchOfLastRenderQueueTransition;
	std::unordered_map<std::wstring, unsigned int>  batchOfLastComputeQueueTransition;

	std::unordered_map<std::wstring, unsigned int>  batchOfLastRenderQueueProducer;
	std::unordered_map<std::wstring, unsigned int>  batchOfLastComputeQueueProducer;

	unsigned int currentBatchIndex = 0;
    for (auto& passAndResources : passes) {
        bool needsNewBatch = false;

        // Determine if a new batch is needed based on resource state conflicts
        switch (passAndResources.type) {
            case PassType::Render: {
            auto& renderPass = std::get<RenderPassAndResources>(passAndResources.pass);
                if (IsNewBatchNeeded(currentBatch, renderPass, computeUAVs)) {
                    // Compute transitions for the current batch
                    // During execution, finalResourceStates contains the state of
                    // each resource before the current batch, if it has been used
                    //ComputeTransitionsForBatch(currentBatch, finalResourceStates);
                    for (const auto& [resourceName, state] : currentBatch.resourceStates) {
                        finalResourceStates[resourceName] = state;
                    }
                    batches.push_back(std::move(currentBatch));
                    currentBatch = PassBatch();
					currentBatch.renderTransitionFenceValue = GetNextGraphicsQueueFenceValue();
					currentBatch.renderCompletionFenceValue = GetNextGraphicsQueueFenceValue();
					currentBatch.computeTransitionFenceValue = GetNextComputeQueueFenceValue();
					currentBatch.computeCompletionFenceValue = GetNextComputeQueueFenceValue();
                    currentBatchIndex++;
                    // Update final resource states
                }
				auto transitions = UpdateFinalResourceStatesAndGatherTransitionsForPass(finalResourceStates, finalResourceSyncStates, batchOfLastRenderQueueTransition, batchOfLastRenderQueueProducer, renderPass, currentBatchIndex);
				currentBatch.renderTransitions.insert(currentBatch.renderTransitions.end(), transitions.begin(), transitions.end());
                
				// Build synchronization points
				auto [lastComputeTransitionBatch, lastComputeProducerBatch] = GetBatchesToWaitOn(renderPass, batchOfLastComputeQueueTransition, batchOfLastComputeQueueProducer);
                
                if (lastComputeTransitionBatch != -1) {
                    // If the compute queue will transition a resource we need, wait on that transition
                    if (lastComputeTransitionBatch == currentBatchIndex) {
                        currentBatch.computeTransitionSignal = true;
                        currentBatch.renderQueueWaitOnComputeQueueBeforeExecution = true; // Wait to execute
                        currentBatch.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue = currentBatch.computeTransitionFenceValue;
                    } // 
                    else {
                        batches[lastComputeTransitionBatch].computeCompletionSignal = true;
						currentBatch.renderQueueWaitOnComputeQueueBeforeTransition = true; // Wait to transition
						currentBatch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue = (std::max)(currentBatch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue, batches[lastComputeTransitionBatch].computeCompletionFenceValue);
                    }
                }

                if (lastComputeProducerBatch != -1) {
					// A resource cannot be produced and consumed in the same batch
#if defined(_DEBUG)
                    if (lastComputeProducerBatch == currentBatchIndex) {
						spdlog::error("Producer batch is the same as current batch");
						__debugbreak();
                    }
#endif
					batches[lastComputeProducerBatch].computeCompletionSignal = true;
					currentBatch.renderQueueWaitOnComputeQueueBeforeTransition = true; // Wait to transition
					currentBatch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue = (std::max)(currentBatch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue, batches[lastComputeProducerBatch].computeCompletionFenceValue);
                }

                currentBatch.renderPasses.push_back(renderPass);
                // Update desired resource states
                UpdateDesiredResourceStates(currentBatch, renderPass, renderUAVs);
                break;
            }
            case PassType::Compute: {
                auto& computePass = std::get<ComputePassAndResources>(passAndResources.pass);
				if (IsNewBatchNeeded(currentBatch, computePass, renderUAVs)) {
					// Compute transitions for the current batch
					//ComputeTransitionsForBatch(currentBatch, finalResourceStates);
					for (const auto& [resourceName, state] : currentBatch.resourceStates) {
						finalResourceStates[resourceName] = state;
					}
					batches.push_back(std::move(currentBatch));
					currentBatch = PassBatch();
					currentBatch.renderTransitionFenceValue = GetNextGraphicsQueueFenceValue();
					currentBatch.renderCompletionFenceValue = GetNextGraphicsQueueFenceValue();
					currentBatch.computeTransitionFenceValue = GetNextComputeQueueFenceValue();
					currentBatch.computeCompletionFenceValue = GetNextComputeQueueFenceValue();
                    currentBatchIndex++;
				}
				auto transitions = UpdateFinalResourceStatesAndGatherTransitionsForPass(finalResourceStates, finalResourceSyncStates, batchOfLastComputeQueueTransition, batchOfLastComputeQueueProducer, computePass, currentBatchIndex);
                currentBatch.computeTransitions.insert(currentBatch.computeTransitions.end(), transitions.begin(), transitions.end());

				// Build synchronization points
				auto [lastRenderTransitionBatch, lastRenderProducerBatch] = GetBatchesToWaitOn(computePass, batchOfLastRenderQueueTransition, batchOfLastRenderQueueProducer);

                if (lastRenderTransitionBatch != -1) {
                    // If the render queue will transition a resource we need, wait on that transition
                    if (lastRenderTransitionBatch == currentBatchIndex) {
                        currentBatch.renderTransitionSignal = true;
                        currentBatch.computeQueueWaitOnRenderQueueBeforeExecution = true; // Wait to execute
                        currentBatch.computeQueueWaitOnRenderQueueBeforeExecutionFenceValue = currentBatch.renderTransitionFenceValue;
                    } //
                    else {
                        batches[lastRenderTransitionBatch].renderCompletionSignal = true;
                        currentBatch.computeQueueWaitOnRenderQueueBeforeTransition = true; // Wait to transition
                        currentBatch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue = (std::max)(currentBatch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue, batches[lastRenderTransitionBatch].renderCompletionFenceValue);
                    }
                }

				if (lastRenderProducerBatch != -1) {
					// A resource cannot be produced and consumed in the same batch
#if defined(_DEBUG)
					if (lastRenderProducerBatch == currentBatchIndex) {
						spdlog::error("Producer batch is the same as current batch");
						__debugbreak();
					}
#endif
					batches[lastRenderProducerBatch].renderCompletionSignal = true;
					currentBatch.computeQueueWaitOnRenderQueueBeforeTransition = true; // Wait to transition
					currentBatch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue = (std::max)(currentBatch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue, batches[lastRenderProducerBatch].renderCompletionFenceValue);
				}

                currentBatch.computePasses.push_back(computePass);
				// Update desired resource states
				UpdateDesiredResourceStates(currentBatch, computePass, computeUAVs);
				break;
            }
        }


    }

    // Handle the last batch
    //ComputeTransitionsForBatch(currentBatch, finalResourceStates);
    for (const auto& [resourceName, state] : currentBatch.resourceStates) {
        finalResourceStates[resourceName] = state;
    }
    batches.push_back(std::move(currentBatch));

    // Insert transitions to loop resources back to their initial states
    ComputeResourceLoops(finalResourceStates, finalResourceSyncStates);

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

std::pair<int, int> RenderGraph::GetBatchesToWaitOn(ComputePassAndResources& pass, const std::unordered_map<std::wstring, unsigned int>& transitionHistory, const std::unordered_map<std::wstring, unsigned int>& producerHistory) {
	int latestTransition = -1;
	int latestProducer = -1;

    auto processResource = [&](const std::shared_ptr<Resource>& resource) {
        if (transitionHistory.contains(resource->GetName())) {
            latestTransition = (std::max)(latestTransition, (int)transitionHistory.at(resource->GetName()));
        }
        if (producerHistory.contains(resource->GetName())) {
            latestProducer = (std::max)(latestProducer, (int)producerHistory.at(resource->GetName()));
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

std::pair<int, int> RenderGraph::GetBatchesToWaitOn(RenderPassAndResources& pass, const std::unordered_map<std::wstring, unsigned int>& transitionHistory, const std::unordered_map<std::wstring, unsigned int>& producerHistory) {
    int latestTransition = -1;
    int latestProducer = -1;

    auto processResource = [&](const std::shared_ptr<Resource>& resource) {
        if (transitionHistory.contains(resource->GetName())) {
            latestTransition = (std::max)(latestTransition, (int)transitionHistory.at(resource->GetName()));
        }
        if (producerHistory.contains(resource->GetName())) {
            latestProducer = (std::max)(latestProducer, (int)producerHistory.at(resource->GetName()));
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

std::vector<RenderGraph::ResourceTransition> RenderGraph::UpdateFinalResourceStatesAndGatherTransitionsForPass(std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, ResourceSyncState>& finalResourceSyncStates, std::unordered_map<std::wstring, unsigned int>& transitionHistory, std::unordered_map<std::wstring, unsigned int>& producerHistory, ComputePassAndResources& pass, unsigned int batchIndex) {
	std::vector<ResourceTransition> transitions;

    auto addTransition = [&](std::shared_ptr<Resource>& resource, ResourceState finalState, ResourceSyncState finalSyncState) {
        ResourceState initialState = ResourceState::UNKNOWN;
		ResourceSyncState initialSyncState = ResourceSyncState::NONE;
        
        if (initialResourceStates.contains(resource->GetName())) {
			initialState = initialResourceStates[resource->GetName()];
		}
        if (finalResourceStates.contains(resource->GetName())) {
            initialState = finalResourceStates[resource->GetName()];
        }

		if (finalResourceSyncStates.contains(resource->GetName())) {
			initialSyncState = finalResourceSyncStates[resource->GetName()];
		}

		if (initialState != finalState) {
			transitions.push_back(ResourceTransition(resource, initialState, finalState, initialSyncState, finalSyncState));
			transitionHistory[resource->GetName()] = batchIndex;
		}
        };
    
    for(auto& resource : pass.resources.shaderResources) {
		addTransition(resource, ResourceState::NON_PIXEL_SRV, ResourceSyncState::COMPUTE_SHADING);
        finalResourceStates[resource->GetName()] = ResourceState::NON_PIXEL_SRV;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::COMPUTE_SHADING;
    }
	for (auto& resource : pass.resources.constantBuffers) {
		addTransition(resource, ResourceState::CONSTANT, ResourceSyncState::COMPUTE_SHADING);
		finalResourceStates[resource->GetName()] = ResourceState::CONSTANT;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::COMPUTE_SHADING;
	}
	for (auto& resource : pass.resources.unorderedAccessViews) {
		addTransition(resource, ResourceState::UNORDERED_ACCESS, ResourceSyncState::COMPUTE_SHADING);
		finalResourceStates[resource->GetName()] = ResourceState::UNORDERED_ACCESS;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::COMPUTE_SHADING;
		producerHistory[resource->GetName()] = batchIndex;
	}
	return transitions;
}

std::vector<RenderGraph::ResourceTransition> RenderGraph::UpdateFinalResourceStatesAndGatherTransitionsForPass(std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, ResourceSyncState>& finalResourceSyncStates, std::unordered_map<std::wstring, unsigned int>& transitionHistory, std::unordered_map<std::wstring, unsigned int>& producerHistory, RenderPassAndResources& pass, unsigned int batchIndex) {
    std::vector<ResourceTransition> transitions;

    auto addTransition = [&](std::shared_ptr<Resource>& resource, ResourceState finalState, ResourceSyncState finalSyncState) {
        ResourceState initialState = ResourceState::UNKNOWN;
		ResourceSyncState initialSyncState = ResourceSyncState::NONE;

        if (initialResourceStates.contains(resource->GetName())) {
            initialState = initialResourceStates[resource->GetName()];
        }
        if (finalResourceStates.contains(resource->GetName())) {
            initialState = finalResourceStates[resource->GetName()];
        }

		if (finalResourceSyncStates.contains(resource->GetName())) {
			initialSyncState = finalResourceSyncStates[resource->GetName()];
		}

        if (initialState != finalState) {
            transitions.push_back(ResourceTransition(resource, initialState, finalState, initialSyncState, finalSyncState));
			transitionHistory[resource->GetName()] = batchIndex;
        }
        };

    for (auto& resource : pass.resources.shaderResources) {
		addTransition(resource, ResourceState::ALL_SRV, ResourceSyncState::DRAW); // TODO: Use fine-grained sync states
		finalResourceStates[resource->GetName()] = ResourceState::ALL_SRV;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::DRAW;
	}
	for (auto& resource : pass.resources.renderTargets) {
		addTransition(resource, ResourceState::RENDER_TARGET, ResourceSyncState::DRAW);
		finalResourceStates[resource->GetName()] = ResourceState::RENDER_TARGET;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::DRAW;
		producerHistory[resource->GetName()] = batchIndex;
	}
	for (auto& resource : pass.resources.depthTextures) {
		addTransition(resource, ResourceState::DEPTH_WRITE, ResourceSyncState::DRAW);
		finalResourceStates[resource->GetName()] = ResourceState::DEPTH_WRITE;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::DRAW;
		producerHistory[resource->GetName()] = batchIndex;
	}
	for (auto& resource : pass.resources.constantBuffers) {
		addTransition(resource, ResourceState::CONSTANT, ResourceSyncState::DRAW);
		finalResourceStates[resource->GetName()] = ResourceState::CONSTANT;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::DRAW;
	}
	for (auto& resource : pass.resources.unorderedAccessViews) {
		addTransition(resource, ResourceState::UNORDERED_ACCESS, ResourceSyncState::DRAW);
		finalResourceStates[resource->GetName()] = ResourceState::UNORDERED_ACCESS;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::DRAW;
		producerHistory[resource->GetName()] = batchIndex;
	}
	for (auto& resource : pass.resources.copyTargets) {
		addTransition(resource, ResourceState::COPY_DEST, ResourceSyncState::COPY);
		finalResourceStates[resource->GetName()] = ResourceState::COPY_DEST;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::COPY;
		producerHistory[resource->GetName()] = batchIndex;
	}
	for (auto& resource : pass.resources.copySources) {
		addTransition(resource, ResourceState::COPY_SOURCE, ResourceSyncState::COPY);
		finalResourceStates[resource->GetName()] = ResourceState::COPY_SOURCE;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::COPY;
	}
	for (auto& resource : pass.resources.indirectArgumentBuffers) {
		addTransition(resource, ResourceState::INDIRECT_ARGUMENT, ResourceSyncState::DRAW);
		finalResourceStates[resource->GetName()] = ResourceState::INDIRECT_ARGUMENT;
		finalResourceSyncStates[resource->GetName()] = ResourceSyncState::DRAW;
	}
	return transitions;
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
		auto& resource = resourcesByName[resourcePair.first];
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
    if(name == L"") {
		throw std::runtime_error("Resource name cannot be empty");
	}
	else if (resourcesByName.find(name) != resourcesByName.end()) {
		throw std::runtime_error("Resource with name " + ws2s(name) + " already exists");
	}
#endif
    resourcesByName[name] = resource;
	if (transition) {
		initialResourceStates[name] = initialState;
	}
}

std::shared_ptr<Resource> RenderGraph::GetResourceByName(const std::wstring& name) {
	return resourcesByName[name];
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
			auto& transitions = transition.pResource->GetEnhancedBarrierGroup(transition.fromState, transition.toState, transition.prevSyncState, transition.newSyncState);
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
            auto& transitions = transition.pResource->GetEnhancedBarrierGroup(transition.fromState, transition.toState, transition.prevSyncState, transition.newSyncState);
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
        if (batch.renderCompletionSignal) {
            graphicsQueue->Signal(m_graphicsQueueFence.Get(), currentGraphicsQueueFenceOffset+batch.renderCompletionFenceValue);
        }
    }
}

bool RenderGraph::IsNewBatchNeeded(PassBatch& currentBatch, const RenderPassAndResources& passAndResources, const std::unordered_set<std::wstring>& computeUAVs) {
	// New batch is needed if (a) current batch has a resource we need for this pass in a different state
	// Or (b) if this pass would use a resource in a manner that would cause a cross-queue read/write hazard
    for (auto& resource : passAndResources.resources.shaderResources) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::ALL_SRV)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.renderTargets) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::RENDER_TARGET)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.depthTextures) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::DEPTH_WRITE)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::CONSTANT)) {
            return true;
        }
    }
	for (auto& resource : passAndResources.resources.unorderedAccessViews) {
		if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::UNORDERED_ACCESS)) {
			return true;
		}
		if (computeUAVs.contains(resource->GetName())) {
			return true;
		}
	}
	for (auto& resource : passAndResources.resources.copySources) {
		if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::COPY_SOURCE)) {
			return true;
		}
	}
    for (auto& resource : passAndResources.resources.copyTargets) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::COPY_DEST)) {
            return true;
        }
    }
	for (auto& resource : passAndResources.resources.indirectArgumentBuffers) {
		if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::INDIRECT_ARGUMENT)) {
			return true;
		}
	}
    return false;
}

bool RenderGraph::IsNewBatchNeeded(PassBatch& currentBatch, const ComputePassAndResources& passAndResources, const std::unordered_set<std::wstring>& renderUAVs) {
    // New batch is needed if (a) current batch has a resource we need for this pass in a different state
    // Or (b) if this pass would use a resource in a manner that would cause a cross-queue read/write hazard
    for (auto& resource : passAndResources.resources.shaderResources) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::NON_PIXEL_SRV)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::CONSTANT)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.unorderedAccessViews) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::UNORDERED_ACCESS)) {
            return true;
        }
		if (renderUAVs.contains(resource->GetName())) {
			return true;
		}
    }
    return false;
}

//void RenderGraph::ComputeTransitionsForBatch(PassBatch& batch, const std::unordered_map<std::wstring, ResourceState>& previousStates) {
//    for (const auto& [resourceName, requiredState] : batch.resourceStates) {
//        ResourceState previousState = ResourceState::UNKNOWN;
//        auto it = previousStates.find(resourceName);
//        std::shared_ptr<Resource> resource = GetResourceByName(resourceName);
//        if (it != previousStates.end()) {
//            previousState = it->second;
//        }
//        else {
//            
//            // Use the resource's current state
//            if (resource) {
//                previousState = resource->GetState();
//            }
//            else {
//                throw std::runtime_error(ws2s(L"Resource not found: " + resourceName));
//            }
//        }
//        if (previousState != requiredState) {
//            batch.renderTransitions.push_back({ resource, previousState, requiredState });
//        }
//    }
//}

void RenderGraph::UpdateDesiredResourceStates(PassBatch& batch, RenderPassAndResources& passAndResources, std::unordered_set<std::wstring>& renderUAVs) {
    // Update batch.resourceStates based on the resources used in passAndResources
    for (auto& resource : passAndResources.resources.shaderResources) {
        batch.resourceStates[resource->GetName()] = ResourceState::ALL_SRV;
    }
    for (auto& resource : passAndResources.resources.renderTargets) {
        batch.resourceStates[resource->GetName()] = ResourceState::RENDER_TARGET;
    }
    for (auto& resource : passAndResources.resources.depthTextures) {
        batch.resourceStates[resource->GetName()] = ResourceState::DEPTH_WRITE;
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        batch.resourceStates[resource->GetName()] = ResourceState::CONSTANT;
    }
    for (auto& resource : passAndResources.resources.unorderedAccessViews) {
		batch.resourceStates[resource->GetName()] = ResourceState::UNORDERED_ACCESS;
		renderUAVs.insert(resource->GetName());
    }
	for (auto& resource : passAndResources.resources.copySources) {
		batch.resourceStates[resource->GetName()] = ResourceState::COPY_SOURCE;
	}
	for (auto& resource : passAndResources.resources.copyTargets) {
		batch.resourceStates[resource->GetName()] = ResourceState::COPY_DEST;
	}
	for (auto& resource : passAndResources.resources.indirectArgumentBuffers) {
		batch.resourceStates[resource->GetName()] = ResourceState::INDIRECT_ARGUMENT;
	}
}

void RenderGraph::UpdateDesiredResourceStates(PassBatch& batch, ComputePassAndResources& passAndResources, std::unordered_set<std::wstring>& computeUAVs) {
    // Update batch.resourceStates based on the resources used in passAndResources
    for (auto& resource : passAndResources.resources.shaderResources) {
        batch.resourceStates[resource->GetName()] = ResourceState::NON_PIXEL_SRV;
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        batch.resourceStates[resource->GetName()] = ResourceState::CONSTANT;
    }
    for (auto& resource : passAndResources.resources.unorderedAccessViews) {
        batch.resourceStates[resource->GetName()] = ResourceState::UNORDERED_ACCESS;
        computeUAVs.insert(resource->GetName());
    }
}


void RenderGraph::ComputeResourceLoops(const std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, ResourceSyncState>& finalResourceSyncStates) {
	PassBatch loopBatch;
    for (const auto& [resourceName, finalState] : finalResourceStates) {
        auto resource = GetResourceByName(resourceName);
        if (!resource) {
            throw std::runtime_error(ws2s(L"Resource not found: " + resourceName));
        }

        ResourceState initialState = resource->GetState();
        if (finalState != initialState) {
            // Insert a transition to bring the resource back to its initial state
            ResourceTransition transition = { resource, finalState, initialState, finalResourceSyncStates[resource->GetName()], ResourceSyncState::NONE};
            loopBatch.renderTransitions.push_back(transition);
        }
    }
	batches.push_back(std::move(loopBatch));
}