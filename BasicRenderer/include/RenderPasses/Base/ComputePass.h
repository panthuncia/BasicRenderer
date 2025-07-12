#pragma once

#include <vector>
#include <directx/d3d12.h>
#include <wrl/client.h>
#include <unordered_set>

#include "Resources/Resource.h"
#include "Render/RenderContext.h"
#include "Resources/ResourceStates.h"
#include "Render/ResourceRequirements.h"
#include "RenderPasses/Base/PassReturn.h"
#include "Resources/ResourceStateTracker.h"
#include "Resources/ResourceIdentifier.h"
#include "Render/ResourceRegistry.h"
#include "../../../generated/BuiltinResources.h"
#include "ResourceDescriptorIndexHelper.h"
#include "Render/PipelineState.h"

struct ComputePassParameters {
	std::vector<ResourceAndRange> shaderResources;
	std::vector<ResourceAndRange> constantBuffers;
	std::vector<ResourceAndRange> unorderedAccessViews;
	std::vector<ResourceAndRange> indirectArgumentBuffers;
	std::vector<ResourceAndRange> legacyInteropResources;
	std::vector<std::pair<ResourceAndRange, ResourceState>> internalTransitions;

	std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher> identifierSet;
	std::vector<ResourceRequirement> resourceRequirements;
};

class ComputePassBuilder;

class ComputePass {
public:
	virtual ~ComputePass() = default;

	void SetResourceRegistryView(std::shared_ptr<ResourceRegistryView> resourceRegistryView) {
		m_resourceRegistryView = resourceRegistryView;
		m_resourceDescriptorIndexHelper = std::make_unique<ResourceDescriptorIndexHelper>(resourceRegistryView);
	}
	virtual void Setup() = 0;
	virtual void RegisterCommandLists(std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> commandLists) {};

	virtual void Update() {};
	virtual PassReturn Execute(RenderContext& context) = 0;
	virtual void Cleanup(RenderContext& context) = 0;

	void Invalidate() { invalidated = true; }
	bool IsInvalidated() const { return invalidated; }

protected:
	bool invalidated = true;
	virtual void DeclareResourceUsages(ComputePassBuilder* builder) {};

	void BindResourceDescriptorIndices(ID3D12GraphicsCommandList7* commandList, const PipelineResources& resources) {
		unsigned int indices[NumResourceDescriptorIndicesRootConstants] = {};
		int i = 0;
		for (auto& binding : resources.mandatoryResourceDescriptorSlots) {
			indices[i] = m_resourceDescriptorIndexHelper->GetResourceDescriptorIndex(binding.hash, false, &binding.name);
			i++;
		}
		for (auto& binding : resources.optionalResourceDescriptorSlots) {
			indices[i] = m_resourceDescriptorIndexHelper->GetResourceDescriptorIndex(binding.hash, true, &binding.name);
			i++;
		}
		commandList->SetComputeRoot32BitConstants(
			ResourceDescriptorIndicesRootSignatureIndex,
			i,
			indices,
			0
		);
	}

	void RegisterSRV(SRVViewType type, ResourceIdentifier id, unsigned int mip = 0, unsigned int slice = 0) {
		m_resourceDescriptorIndexHelper->RegisterSRV(type, id, mip, slice);
	}
	void RegisterSRV(ResourceIdentifier id, unsigned int mip = 0, unsigned int slice = 0) {
		m_resourceDescriptorIndexHelper->RegisterSRV(id, mip, slice);
	}
	void RegisterUAV(ResourceIdentifier id, unsigned int mip = 0, unsigned int slice = 0) {
		m_resourceDescriptorIndexHelper->RegisterUAV(id, mip, slice);
	}

	std::unique_ptr<ResourceDescriptorIndexHelper> m_resourceDescriptorIndexHelper;
	std::shared_ptr<ResourceRegistryView> m_resourceRegistryView;
	friend class ComputePassBuilder;
};