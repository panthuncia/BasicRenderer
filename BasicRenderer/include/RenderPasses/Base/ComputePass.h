#pragma once

#include <vector>
#include <unordered_set>
#include <rhi.h>

#include "Resources/Resource.h"
#include "Render/ResourceRequirements.h"
#include "RenderPasses/Base/PassReturn.h"
#include "Resources/ResourceStateTracker.h"
#include "Resources/ResourceIdentifier.h"
#include "Render/ResourceRegistry.h"
#include "ResourceDescriptorIndexHelper.h"
#include "Render/PipelineState.h"
#include "interfaces/IResourceProvider.h"
#include "Render/PassInputs.h"
#include "ShaderBuffers.h"
#include "Render/RenderContext.h"

struct ComputePassParameters {
	std::vector<ResourceHandleAndRange> shaderResources;
	std::vector<ResourceHandleAndRange> constantBuffers;
	std::vector<ResourceHandleAndRange> unorderedAccessViews;
	std::vector<ResourceHandleAndRange> indirectArgumentBuffers;
	std::vector<ResourceHandleAndRange> legacyInteropResources;
	std::vector<std::pair<ResourceHandleAndRange, ResourceState>> internalTransitions;

	std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher> identifierSet;
	std::vector<ResourceRequirement> staticResourceRequirements; // Static resource requirements for the pass
	std::vector<ResourceRequirement> frameResourceRequirements; // Resource requirements that may change each frame + static ones
};

class ComputePassBuilder;

class ComputePass : public IResourceProvider, public RenderGraphPassBase {
public:
	virtual ~ComputePass() = default;

	void SetResourceRegistryView(std::shared_ptr<ResourceRegistryView> resourceRegistryView) {
		m_resourceRegistryView = resourceRegistryView;
		m_resourceDescriptorIndexHelper = std::make_unique<ResourceDescriptorIndexHelper>(resourceRegistryView);
	}

	virtual void Setup() = 0;

	virtual void Update(const UpdateContext& context) {};
	virtual void ExecuteImmediate(ImmediateContext& context) {};
	virtual PassReturn Execute(RenderContext& context) { return {}; };
	virtual void Cleanup() = 0;

	void Invalidate() { invalidated = true; }
	bool IsInvalidated() const { return invalidated; }

protected:
	bool invalidated = true;
	virtual void DeclareResourceUsages(ComputePassBuilder* builder) {};

	void BindResourceDescriptorIndices(rhi::CommandList& commandList, const PipelineResources& resources) {
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
		if (i > 0) {
			commandList.PushConstants(rhi::ShaderStage::Compute, 0, ResourceDescriptorIndicesRootSignatureIndex, 0, i, indices);
		}
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
	void RegisterCBV(ResourceIdentifier id) {
		m_resourceDescriptorIndexHelper->RegisterCBV(id);
	}

	virtual std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) { return nullptr; }
	virtual std::vector<ResourceIdentifier> GetSupportedKeys() { return {}; }

	std::unique_ptr<ResourceDescriptorIndexHelper> m_resourceDescriptorIndexHelper;
	std::shared_ptr<ResourceRegistryView> m_resourceRegistryView;
	friend class ComputePassBuilder;
};