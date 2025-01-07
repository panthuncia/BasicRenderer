#include "RenderGraph.h"

#include "RenderContext.h"
#include "utilities.h"
#include "SettingsManager.h"

static bool mapHasResourceNotInState(std::unordered_map<std::wstring, ResourceState>& map, std::wstring resourceName, ResourceState state) {
    return mapHasKeyNotAsValue<std::wstring, ResourceState>(map, resourceName, state);
}

static bool mapHasResourceInUAVState(std::unordered_map<std::wstring, ResourceState>& map, std::wstring resourceName) {
	bool a = mapHasKeyAsValue<std::wstring, ResourceState>(map, resourceName, ResourceState::ALL_SRV);
}

void RenderGraph::Compile() {
    batches.clear();
    auto currentBatch = PassBatch();
    //std::unordered_map<std::wstring, ResourceState> previousBatchResourceStates;
    std::unordered_map<std::wstring, ResourceState> finalResourceStates;

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
                if (IsNewBatchNeeded(currentBatch, passAndResources.renderPass, computeUAVs)) {
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
				auto transitions = UpdateFinalResourceStatesAndGatherTransitionsForPass(finalResourceStates, batchOfLastRenderQueueTransition, batchOfLastRenderQueueProducer, passAndResources.renderPass, currentBatchIndex);
				currentBatch.renderTransitions.insert(currentBatch.renderTransitions.end(), transitions.begin(), transitions.end());
                
				// Build synchronization points
				auto [lastComputeTransitionBatch, lastComputeProducerBatch] = GetBatchesToWaitOn(passAndResources.renderPass, batchOfLastComputeQueueTransition, batchOfLastComputeQueueProducer);
                
                if (lastComputeTransitionBatch != 0) {
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

                if (lastComputeProducerBatch != 0) {
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

                currentBatch.renderPasses.push_back(passAndResources.renderPass);
                // Update desired resource states
                UpdateDesiredResourceStates(currentBatch, passAndResources.renderPass, renderUAVs);
                break;
            }
            case PassType::Compute: {
				if (IsNewBatchNeeded(currentBatch, passAndResources.computePass, renderUAVs)) {
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
				auto transitions = UpdateFinalResourceStatesAndGatherTransitionsForPass(finalResourceStates, batchOfLastComputeQueueTransition, batchOfLastComputeQueueProducer, passAndResources.computePass, currentBatchIndex);
                currentBatch.computeTransitions.insert(currentBatch.computeTransitions.end(), transitions.begin(), transitions.end());

				// Build synchronization points
				auto [lastRenderTransitionBatch, lastRenderProducerBatch] = GetBatchesToWaitOn(passAndResources.computePass, batchOfLastRenderQueueTransition, batchOfLastRenderQueueProducer);

                if (lastRenderTransitionBatch != 0) {
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

				if (lastRenderProducerBatch != 0) {
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

                currentBatch.computePasses.push_back(passAndResources.computePass);
				// Update desired resource states
				UpdateDesiredResourceStates(currentBatch, passAndResources.computePass, computeUAVs);
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
    ComputeResourceLoops(finalResourceStates);
}

std::pair<int, int> RenderGraph::GetBatchesToWaitOn(ComputePassAndResources& pass, const std::unordered_map<std::wstring, unsigned int>& transitionHistory, const std::unordered_map<std::wstring, unsigned int>& producerHistory) {
	int latestTransition = -1;
	int latestProducer = -1;

    auto processResource = [&](const std::shared_ptr<Resource>& resource) {
        if (transitionHistory.contains(resource->GetName())) {
            latestTransition = (std::max)(latestTransition, transitionHistory.at(resource->GetName()));
        }
        if (producerHistory.contains(resource->GetName())) {
            latestProducer = (std::max)(latestProducer, producerHistory.at(resource->GetName()));
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
            latestTransition = (std::max)(latestTransition, transitionHistory.at(resource->GetName()));
        }
        if (producerHistory.contains(resource->GetName())) {
            latestProducer = (std::max)(latestProducer, producerHistory.at(resource->GetName()));
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

std::vector<RenderGraph::ResourceTransition> RenderGraph::UpdateFinalResourceStatesAndGatherTransitionsForPass(std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, unsigned int> transitionHistory, std::unordered_map<std::wstring, unsigned int> producerHistory, ComputePassAndResources pass, unsigned int batchIndex) {
	std::vector<ResourceTransition> transitions;

    auto addTransition = [&](std::shared_ptr<Resource>& resource, ResourceState finalState) {
		ResourceState initialState = ResourceState::UNKNOWN;
        if (finalResourceStates.contains(resource->GetName())) {
            initialState = finalResourceStates[resource->GetName()];
        }
		if (initialState != finalState) {
			transitions.push_back(ResourceTransition(resource, initialState, finalState));
			transitionHistory[resource->GetName()] = batchIndex;
		}
        };

    for(auto& resource : pass.resources.shaderResources) {
		addTransition(resource, ResourceState::ALL_SRV);
        finalResourceStates[resource->GetName()] = ResourceState::ALL_SRV;
    }
	for (auto& resource : pass.resources.constantBuffers) {
		addTransition(resource, ResourceState::CONSTANT);
		finalResourceStates[resource->GetName()] = ResourceState::CONSTANT;
	}
	for (auto& resource : pass.resources.unorderedAccessViews) {
		addTransition(resource, ResourceState::UNORDERED_ACCESS);
		finalResourceStates[resource->GetName()] = ResourceState::UNORDERED_ACCESS;
		producerHistory[resource->GetName()] = batchIndex;
	}
	return transitions;
}

std::vector<RenderGraph::ResourceTransition> RenderGraph::UpdateFinalResourceStatesAndGatherTransitionsForPass(std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, unsigned int> transitionHistory, std::unordered_map<std::wstring, unsigned int> producerHistory, RenderPassAndResources pass, unsigned int batchIndex) {
    std::vector<ResourceTransition> transitions;

    auto addTransition = [&](std::shared_ptr<Resource>& resource, ResourceState finalState) {
        ResourceState initialState = ResourceState::UNKNOWN;
        if (finalResourceStates.contains(resource->GetName())) {
            initialState = finalResourceStates[resource->GetName()];
        }
        if (initialState != finalState) {
            transitions.push_back(ResourceTransition(resource, initialState, finalState));
			transitionHistory[resource->GetName()] = batchIndex;
        }
        };

    for (auto& resource : pass.resources.shaderResources) {
		addTransition(resource, ResourceState::ALL_SRV);
		finalResourceStates[resource->GetName()] = ResourceState::ALL_SRV;
	}
	for (auto& resource : pass.resources.renderTargets) {
		addTransition(resource, ResourceState::RENDER_TARGET);
		finalResourceStates[resource->GetName()] = ResourceState::RENDER_TARGET;
		producerHistory[resource->GetName()] = batchIndex;
	}
	for (auto& resource : pass.resources.depthTextures) {
		addTransition(resource, ResourceState::DEPTH_WRITE);
		finalResourceStates[resource->GetName()] = ResourceState::DEPTH_WRITE;
		producerHistory[resource->GetName()] = batchIndex;
	}
	for (auto& resource : pass.resources.constantBuffers) {
		addTransition(resource, ResourceState::CONSTANT);
		finalResourceStates[resource->GetName()] = ResourceState::CONSTANT;
	}
	for (auto& resource : pass.resources.unorderedAccessViews) {
		addTransition(resource, ResourceState::UNORDERED_ACCESS);
		finalResourceStates[resource->GetName()] = ResourceState::UNORDERED_ACCESS;
		producerHistory[resource->GetName()] = batchIndex;
	}
	for (auto& resource : pass.resources.copyTargets) {
		addTransition(resource, ResourceState::COPY_DEST);
		finalResourceStates[resource->GetName()] = ResourceState::COPY_DEST;
		producerHistory[resource->GetName()] = batchIndex;
	}
	for (auto& resource : pass.resources.copySources) {
		addTransition(resource, ResourceState::COPY_SOURCE);
		finalResourceStates[resource->GetName()] = ResourceState::COPY_SOURCE;
	}
	for (auto& resource : pass.resources.indirectArgumentBuffers) {
		addTransition(resource, ResourceState::INDIRECT_ARGUMENT);
		finalResourceStates[resource->GetName()] = ResourceState::INDIRECT_ARGUMENT;
	}
	return transitions;
}



void RenderGraph::Setup(ID3D12CommandQueue* queue) {
    for (auto& pass : passes) {
        switch (pass.type) {
        case PassType::Render:
            pass.renderPass.pass->Setup();
            break;
        case PassType::Compute:
            pass.computePass.pass->Setup();
            break;
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
        m_commandAllocators.push_back(allocator);
        m_transitionCommandLists.push_back(commandList);
    }
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_graphicsQueueFence));
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeQueueFence));
}

void RenderGraph::AddRenderPass(std::shared_ptr<RenderPass> pass, RenderPassParameters& resources, std::string name) {
    RenderPassAndResources passAndResources;
    passAndResources.pass = pass;
    passAndResources.resources = resources;
	AnyPassAndResources passAndResourcesAny;
	passAndResourcesAny.type = PassType::Render;
	passAndResourcesAny.renderPass = passAndResources;
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
	passAndResourcesAny.computePass = passAndResources;
	passes.push_back(passAndResourcesAny);
	if (name != "") {
		computePassesByName[name] = pass;
	}
}

void RenderGraph::AddResource(std::shared_ptr<Resource> resource) {
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
	auto& queue = manager.GetCommandQueue();
	auto& transitionCommandList = m_transitionCommandLists[context.frameIndex];
	auto& commandAllocator = m_commandAllocators[context.frameIndex];

    UINT64 currentGraphicsQueueFenceOffset = m_graphicsQueueFenceValue;
	UINT64 currentComputeQueueFenceOffset = m_computeQueueFenceValue;

    commandAllocator->Reset();
    for (auto& batch : batches) {

		// Wait on compute queue if needed
		if (batch.renderQueueWaitOnComputeQueueBeforeTransition) {
			queue->Wait(m_computeQueueFence.Get(), batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue);
		}

        // Perform resource transitions
		//TODO: If a pass is cached, we can skip the transitions, but we may need a new set
        transitionCommandList->Reset(commandAllocator.Get(), NULL);
		std::vector<D3D12_RESOURCE_BARRIER> barriers;
        for (auto& transition : batch.renderTransitions) {
            auto& transitions = transition.pResource->GetTransitions(transition.fromState, transition.toState);
            for (auto& barrier : transitions) {
				barriers.push_back(barrier);
            }
        }
        if (barriers.size() > 0) {
            transitionCommandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }
        transitionCommandList->Close();
        ID3D12CommandList* ppCommandLists[] = { transitionCommandList.Get()};
        queue->ExecuteCommandLists(1, ppCommandLists);
        if (batch.renderTransitionSignal) {
			queue->Signal(m_graphicsQueueFence.Get(), batch.renderTransitionFenceValue);
        }


		// Wait on render queue if needed
		if (batch.renderQueueWaitOnComputeQueueBeforeExecution) {
			queue->Wait(m_computeQueueFence.Get(), batch.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue);
		}

        // Execute all passes in the batch
        for (auto& passAndResources : batch.renderPasses) {
			if (passAndResources.pass->IsInvalidated()) {

                auto passReturn = passAndResources.pass->Execute(context);
				ID3D12CommandList** ppCommandLists = reinterpret_cast<ID3D12CommandList**>(passReturn.commandLists.data());
                queue->ExecuteCommandLists(static_cast<UINT>(passReturn.commandLists.size()), ppCommandLists);

                if (passReturn.fence != nullptr) {
					queue->Signal(passReturn.fence, passReturn.fenceValue); // External fences for readback: TODO merge with new fence system
                }
			}
        }
        if (batch.renderCompletionSignal) {
            queue->Signal(m_graphicsQueueFence.Get(), batch.renderCompletionFenceValue);
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
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::ALL_SRV)) {
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

void RenderGraph::ComputeTransitionsForBatch(PassBatch& batch, const std::unordered_map<std::wstring, ResourceState>& previousStates) {
    for (const auto& [resourceName, requiredState] : batch.resourceStates) {
        ResourceState previousState = ResourceState::UNKNOWN;
        auto it = previousStates.find(resourceName);
        std::shared_ptr<Resource> resource = GetResourceByName(resourceName);
        if (it != previousStates.end()) {
            previousState = it->second;
        }
        else {
            
            // Use the resource's current state
            if (resource) {
                previousState = resource->GetState();
            }
            else {
                throw std::runtime_error(ws2s(L"Resource not found: " + resourceName));
            }
        }
        if (previousState != requiredState) {
            batch.renderTransitions.push_back({ resource, previousState, requiredState });
        }
    }
}

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
        batch.resourceStates[resource->GetName()] = ResourceState::ALL_SRV;
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        batch.resourceStates[resource->GetName()] = ResourceState::CONSTANT;
    }
    for (auto& resource : passAndResources.resources.unorderedAccessViews) {
        batch.resourceStates[resource->GetName()] = ResourceState::UNORDERED_ACCESS;
        computeUAVs.insert(resource->GetName());
    }
}


void RenderGraph::ComputeResourceLoops(const std::unordered_map<std::wstring, ResourceState>& finalResourceStates) {
	PassBatch loopBatch;
    for (const auto& [resourceName, finalState] : finalResourceStates) {
        auto resource = GetResourceByName(resourceName);
        if (!resource) {
            throw std::runtime_error(ws2s(L"Resource not found: " + resourceName));
        }

        ResourceState initialState = resource->GetState();
        if (finalState != initialState) {
            // Insert a transition to bring the resource back to its initial state
            ResourceTransition transition = { resource, finalState, initialState };
            loopBatch.renderTransitions.push_back(transition);
        }
    }
	batches.push_back(std::move(loopBatch));
}