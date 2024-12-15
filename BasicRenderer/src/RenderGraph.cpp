#include "RenderGraph.h"
#include "RenderContext.h"
#include "utilities.h"
#include "SettingsManager.h"

static bool mapHasResourceNotInState(std::unordered_map<std::wstring, ResourceState>& map, std::wstring resourceName, ResourceState state) {
    return mapHasKeyNotAsValue<std::wstring, ResourceState>(map, resourceName, state);
}

void RenderGraph::Compile() {
    batches.clear();
    auto currentBatch = PassBatch();
    //std::unordered_map<std::wstring, ResourceState> previousBatchResourceStates;
    std::unordered_map<std::wstring, ResourceState> finalResourceStates;

    for (auto& passAndResources : passes) {
        bool needsNewBatch = false;

        // Determine if a new batch is needed based on resource state conflicts
        if (IsNewBatchNeeded(currentBatch, passAndResources)) {
            // Compute transitions for the current batch
            // During execution, finalResourceStates contains the state of
            // each resource before the current batch, if it has been used
            ComputeTransitionsForBatch(currentBatch, finalResourceStates);
            for (const auto& [resourceName, state] : currentBatch.resourceStates) {
                finalResourceStates[resourceName] = state;
            }
            batches.push_back(std::move(currentBatch));
            currentBatch = PassBatch();
            //previousBatchResourceStates = batches.back().resourceStates;
            // Update final resource states
        }

        currentBatch.passes.push_back(passAndResources);

        // Update desired resource states
        UpdateDesiredResourceStates(currentBatch, passAndResources);

    }

    // Handle the last batch
    ComputeTransitionsForBatch(currentBatch, finalResourceStates);
    for (const auto& [resourceName, state] : currentBatch.resourceStates) {
        finalResourceStates[resourceName] = state;
    }
    batches.push_back(std::move(currentBatch));

    // Insert transitions to loop resources back to their initial states
    ComputeResourceLoops(finalResourceStates);
}

void RenderGraph::Setup(ID3D12CommandQueue* queue) {
    for (auto& pass : passes) {
        pass.pass->Setup();
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
}

void RenderGraph::AddPass(std::shared_ptr<RenderPass> pass, PassParameters& resources, std::string name) {
    PassAndResources passAndResources;
    passAndResources.pass = pass;
    passAndResources.resources = resources;
	passes.push_back(passAndResources);
    if (name != "") {
        passesByName[name] = pass;
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

std::shared_ptr<RenderPass> RenderGraph::GetPassByName(const std::string& name) {
    if (passesByName.find(name)!= passesByName.end()) {
        return passesByName[name];
    }
    else {
        return nullptr;
    }
}

void RenderGraph::Update() {
    for (auto& batch : batches) {
        for (auto& passAndResources : batch.passes) {
            passAndResources.pass->Update();
        }
    }
}

void RenderGraph::Execute(RenderContext& context) {
	auto& manager = DeviceManager::GetInstance();
	auto& queue = manager.GetCommandQueue();
	auto& transitionCommandList = m_transitionCommandLists[context.frameIndex];
	auto& commandAllocator = m_commandAllocators[context.frameIndex];
    commandAllocator->Reset();
    for (auto& batch : batches) {
        // Perform resource transitions
		//TODO: If a pass is cached, we can skip the transitions, but we may need a new set
        transitionCommandList->Reset(commandAllocator.Get(), NULL);
		std::vector<D3D12_RESOURCE_BARRIER> barriers;
        for (auto& transition : batch.transitions) {
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

        // Execute all passes in the batch
        for (auto& passAndResources : batch.passes) {
			if (passAndResources.pass->IsInvalidated()) {
                auto passReturn = passAndResources.pass->Execute(context);
				ID3D12CommandList** ppCommandLists = reinterpret_cast<ID3D12CommandList**>(passReturn.commandLists.data());
				queue->ExecuteCommandLists(static_cast<UINT>(passReturn.commandLists.size()), ppCommandLists);
                if (passReturn.fence != nullptr) {
					queue->Signal(passReturn.fence, passReturn.fenceValue);
                }
			}
        }
    }
}

bool RenderGraph::IsNewBatchNeeded(PassBatch& currentBatch, const PassAndResources& passAndResources) {
    for (auto& resource : passAndResources.resources.shaderResources) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::ALL_SRV)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.renderTargets) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::RENDER_TARGET)) {
            return true;
            break;
        }
    }
    for (auto& resource : passAndResources.resources.depthTextures) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::DEPTH_WRITE)) {
            return true;
            break;
        }
    }
    for (auto& resource : passAndResources.resources.constantBuffers) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::CONSTANT)) {
            return true;
            break;
        }
    }
	for (auto& resource : passAndResources.resources.unorderedAccessViews) {
		if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::UNORDERED_ACCESS)) {
			return true;
			break;
		}
	}
	for (auto& resource : passAndResources.resources.copySources) {
		if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::COPY_SOURCE)) {
			return true;
			break;
		}
	}
    for (auto& resource : passAndResources.resources.copyTargets) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::COPY_DEST)) {
            return true;
            break;
        }
    }
	for (auto& resource : passAndResources.resources.indirectArgumentBuffers) {
		if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::INDIRECT_ARGUMENT)) {
			return true;
			break;
		}
	}
    return false;
}

void RenderGraph::ComputeTransitionsForBatch(PassBatch& batch, const std::unordered_map<std::wstring, ResourceState>& previousStates) {
    for (const auto& [resourceName, requiredState] : batch.resourceStates) {
        if (resourceName == L"AlphaTestPerMeshBuffers") {
            print("?");
        }
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
            batch.transitions.push_back({ resource, previousState, requiredState });
        }
    }
}

void RenderGraph::UpdateDesiredResourceStates(PassBatch& batch, PassAndResources& passAndResources) {
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
            loopBatch.transitions.push_back(transition);
        }
    }
	batches.push_back(std::move(loopBatch));
}