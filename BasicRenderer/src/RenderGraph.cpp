#include "RenderGraph.h"
#include "RenderContext.h"
#include "utilities.h"

static bool mapHasResourceNotInState(std::unordered_map<std::wstring, ResourceState>& map, std::wstring resourceName, ResourceState state) {
    return mapHasKeyNotAsValue<std::wstring, ResourceState>(map, resourceName, state);
}

void RenderGraph::Compile() {
    batches.clear();
    auto currentBatch = PassBatch();
    std::unordered_map<std::wstring, ResourceState> previousBatchResourceStates;
    std::unordered_map<std::wstring, ResourceState> finalResourceStates; // To track final states

    for (auto& passAndResources : passes) {
        bool needsNewBatch = false;

        // Determine if a new batch is needed based on resource state conflicts
        if (IsNewBatchNeeded(currentBatch, passAndResources)) {
            // Compute transitions for the current batch
            ComputeTransitionsForBatch(currentBatch, previousBatchResourceStates);
            batches.push_back(std::move(currentBatch));
            currentBatch = PassBatch();
            previousBatchResourceStates = batches.back().resourceStates;
        }

        currentBatch.passes.push_back(passAndResources);

        // Update desired resource states
        UpdateDesiredResourceStates(currentBatch, passAndResources);

        // Update final resource states
        for (const auto& [resourceName, state] : currentBatch.resourceStates) {
            finalResourceStates[resourceName] = state;
        }
    }

    // Handle the last batch
    ComputeTransitionsForBatch(currentBatch, previousBatchResourceStates);
    batches.push_back(std::move(currentBatch));

    // Insert transitions to loop resources back to their initial states
    ComputeResourceLoops(finalResourceStates);
}

void RenderGraph::Setup(ID3D12CommandQueue* queue, ID3D12CommandAllocator* allocator) {
    for (auto& pass : passes) {
        pass.pass->Setup();
    }
	auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&m_transitionCommandList)));
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
    resourcesByName[resource->GetName()] = resource;
}

std::shared_ptr<Resource> RenderGraph::GetResourceByName(const std::wstring& name) {
	return resourcesByName[name];
}

std::shared_ptr<RenderPass> RenderGraph::GetPassByName(const std::string& name) {
    if (passesByName.contains(name)) {
        return passesByName[name];
    }
    else {
        return nullptr;
    }
}

void RenderGraph::Execute(RenderContext& context) {
	auto& manager = DeviceManager::GetInstance();
	auto& allocator = manager.GetCommandAllocator();
	auto& queue = manager.GetCommandQueue();
    m_transitionCommandList->Reset(allocator.Get(), NULL);
    for (auto& batch : batches) {
        // Perform resource transitions
		//TODO: If a pass is cached, we can skip the transitions, but we may need a new set
        m_transitionCommandList->Reset(allocator.Get(), NULL);
        for (auto& transition : batch.transitions) {
            transition.pResource->Transition(m_transitionCommandList.Get(), transition.fromState, transition.toState);

        }
        m_transitionCommandList->Close();
        ID3D12CommandList* ppCommandLists[] = { m_transitionCommandList.Get()};
        queue->ExecuteCommandLists(1, ppCommandLists);

        // Execute all passes in the batch
        for (auto& passAndResources : batch.passes) {
			if (passAndResources.pass->IsInvalidated()) {
                auto& list = passAndResources.pass->Execute(context);
				ID3D12CommandList** ppCommandLists = list.data();
				queue->ExecuteCommandLists(static_cast<UINT>(list.size()), ppCommandLists);
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
                // Handle error: resource not found
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