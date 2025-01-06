#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <wrl/client.h>

#include "RenderPass.h"
#include "ComputePass.h"
#include "ResourceStates.h"

class Resource;

class RenderGraph {
public:
	void AddRenderPass(std::shared_ptr<RenderPass> pass, RenderPassParameters& resources, std::string name = "");
	void AddComputePass(std::shared_ptr<ComputePass> pass, ComputePassParameters& resources, std::string name = "");
	void Update();
	void Execute(RenderContext& context);
	void Compile();
	void Setup(ID3D12CommandQueue* queue);
	//void AllocateResources(RenderContext& context);
	void AddResource(std::shared_ptr<Resource> resource);
	void CreateResource(std::wstring name);
	std::shared_ptr<Resource> GetResourceByName(const std::wstring& name);
	std::shared_ptr<RenderPass> GetRenderPassByName(const std::string& name);
	std::shared_ptr<ComputePass> GetComputePassByName(const std::string& name);
private:
	struct RenderPassAndResources {
		std::shared_ptr<RenderPass> pass;
		RenderPassParameters resources;
	};

	struct ComputePassAndResources {
		std::shared_ptr<ComputePass> pass;
		ComputePassParameters resources;
	};

	struct ResourceTransition {
		ResourceTransition(std::shared_ptr<Resource> pResource, ResourceState fromState, ResourceState toState)
			: pResource(pResource), fromState(fromState), toState(toState) {
		}
		std::shared_ptr<Resource> pResource;
		ResourceState fromState;
		ResourceState toState;
	};

	enum class CommandQueueType {
		Graphics,
		Compute
	};

	struct PassBatch {
		std::vector<RenderPassAndResources> renderPasses;
		std::vector<ComputePassAndResources> computePasses;
		std::unordered_map<std::wstring, ResourceState> resourceStates; // Desired states in this batch
		std::unordered_map<std::wstring, CommandQueueType> transitionQueue; // Queue to transition resources on
		std::vector<ResourceTransition> renderTransitions; // Transitions needed to reach desired states on the render queue
        std::vector<ResourceTransition> computeTransitions; // Transitions needed to reach desired states on the compute queue

		bool renderQueueWait = false; // Whether to wait on a fence on the render queue
		UINT64 renderQueueWaitFenceValue = 0; // Fence value to wait on
		bool computeQueueWait = false; // Whether to wait on a fence on the compute queue
		UINT64 computeQueueWaitFenceValue = 0; // Fence value to wait on

		bool transitionSignal = false; // Whether to signal a fence after transitions
		UINT64 transitionFenceValue = 0; // Fence value to signal after transitions
		bool completionSignal = false; // Whether to signal a fence after batch completion
		UINT64 completionFenceValue = 0; // Fence value to signal after batch completion
	};

    enum class PassType {
        Unknown,
        Render,
        Compute
    };

    struct AnyPassAndResources {
        PassType type = PassType::Unknown;

        union {
            RenderPassAndResources renderPass;
            ComputePassAndResources computePass;
        };

        AnyPassAndResources() : type(PassType::Unknown) {
            // By default, do not construct anything
        }

        explicit AnyPassAndResources(RenderPassAndResources const& rp)
            : type(PassType::Render) {
            new (&renderPass) RenderPassAndResources(rp);
        }

        explicit AnyPassAndResources(ComputePassAndResources const& cp)
            : type(PassType::Compute) {
            new (&computePass) ComputePassAndResources(cp);
        }

        ~AnyPassAndResources() {
            switch (type) {
            case PassType::Render:
                renderPass.~RenderPassAndResources();
                break;
            case PassType::Compute:
                computePass.~ComputePassAndResources();
                break;
            default:
                // PassType::Unknown => nothing to destroy
                break;
            }
        }

        // Copy/move semantics
        AnyPassAndResources(AnyPassAndResources const& other) {
            type = other.type;
            switch (type) {
            case PassType::Render:
                new (&renderPass) RenderPassAndResources(other.renderPass);
                break;
            case PassType::Compute:
                new (&computePass) ComputePassAndResources(other.computePass);
                break;
            default:
                break;
            }
        }

        AnyPassAndResources& operator=(AnyPassAndResources const& other) {
            if (this == &other)
                return *this;

            // Destroy our active object
            this->~AnyPassAndResources();

            // Recreate from other
            type = other.type;
            switch (type) {
            case PassType::Render:
                new (&renderPass) RenderPassAndResources(other.renderPass);
                break;
            case PassType::Compute:
                new (&computePass) ComputePassAndResources(other.computePass);
                break;
            default:
                break;
            }
            return *this;
        }
    };

	std::vector<AnyPassAndResources> passes;
	std::unordered_map<std::string, std::shared_ptr<RenderPass>> renderPassesByName;
	std::unordered_map<std::string, std::shared_ptr<ComputePass>> computePassesByName;
	std::unordered_map<std::wstring, std::shared_ptr<Resource>> resourcesByName;
	std::vector<PassBatch> batches;

	std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> m_commandAllocators;
	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> m_transitionCommandLists;

	UINT64 m_graphicsQueueFenceValue = 0;
	UINT64 m_computeQueueFenceValue = 0;

	void ComputeTransitionsForBatch(PassBatch& batch, const std::unordered_map<std::wstring, ResourceState>& previousStates);
    void UpdateDesiredResourceStates(PassBatch& batch, RenderPassAndResources& passAndResources, std::unordered_set<std::wstring>& renderUAVs);
    void UpdateDesiredResourceStates(PassBatch& batch, ComputePassAndResources& passAndResources, std::unordered_set<std::wstring>& computeUAVs);

	void ComputeResourceLoops(const std::unordered_map<std::wstring, ResourceState>& finalResourceStates);
	bool IsNewBatchNeeded(PassBatch& currentBatch, const RenderPassAndResources& passAndResources, const std::unordered_set<std::wstring>& computeUAVs);
	bool IsNewBatchNeeded(PassBatch& currentBatch, const ComputePassAndResources& passAndResources, const std::unordered_set<std::wstring>& renderUAVs);

    std::vector<ResourceTransition> UpdateFinalResourceStatesAndGatherTransitionsForPass(std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, unsigned int> transitionHistory, std::unordered_map<std::wstring, unsigned int> producerHistory, ComputePassAndResources pass, unsigned int batchIndex);
	std::vector<ResourceTransition> UpdateFinalResourceStatesAndGatherTransitionsForPass(std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, unsigned int> transitionHistory, std::unordered_map<std::wstring, unsigned int> producerHistory, RenderPassAndResources pass, unsigned int batchIndex);
	std::pair<int, int> GetFencesToWaitOn(ComputePassAndResources& pass, const std::unordered_map<std::wstring, unsigned int>& transitionHistory, const std::unordered_map<std::wstring, unsigned int>& producerHistory);
    std::pair<int, int> GetFencesToWaitOn(RenderPassAndResources& pass, const std::unordered_map<std::wstring, unsigned int>& transitionHistory, const std::unordered_map<std::wstring, unsigned int>& producerHistory);
};