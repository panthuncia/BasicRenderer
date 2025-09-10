#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <rhi.h>

#include "Resources/Resource.h"
#include "Render/RenderContext.h"
#include "Render/ResourceRequirements.h"
#include "RenderPasses/Base/PassReturn.h"
#include "Resources/ResourceStateTracker.h"
#include "Resources/ResourceIdentifier.h"
#include "Render/ResourceRegistry.h"
#include "../../../generated/BuiltinResources.h"
#include "ResourceDescriptorIndexHelper.h"
#include "Render/PipelineState.h"
#include "interfaces/IResourceProvider.h"

struct RenderPassParameters {
    std::vector<ResourceAndRange> shaderResources;
    std::vector<ResourceAndRange> renderTargets;
    std::vector<ResourceAndRange> depthReadResources;
	std::vector<ResourceAndRange> depthReadWriteResources;
	std::vector<ResourceAndRange> constantBuffers;
	std::vector<ResourceAndRange> unorderedAccessViews;
	std::vector<ResourceAndRange> copyTargets;
	std::vector<ResourceAndRange> copySources;
	std::vector<ResourceAndRange> indirectArgumentBuffers;
	std::vector<ResourceAndRange> legacyInteropResources;
	std::vector<std::pair<ResourceAndRange, ResourceState>> internalTransitions;

	std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher> identifierSet;
	std::vector<ResourceRequirement> resourceRequirements;
	bool isGeometryPass = false;
};

class RenderPassBuilder;

class RenderPass : public IResourceProvider {
public:
    virtual ~RenderPass() = default;

	void SetResourceRegistryView(std::shared_ptr<ResourceRegistryView> resourceRegistryView) {
		m_resourceRegistryView = resourceRegistryView;
		m_resourceDescriptorIndexHelper = std::make_unique<ResourceDescriptorIndexHelper>(resourceRegistryView);
	}
    virtual void Setup() = 0;
	virtual void RegisterCommandLists(const std::vector<rhi::CommandList>& commandLists) {};

	virtual void Update() {};
    virtual PassReturn Execute(RenderContext& context) = 0;
    virtual void Cleanup(RenderContext& context) = 0;

	void Invalidate() { invalidated = true; }
	bool IsInvalidated() const { return invalidated; }

protected:
	bool invalidated = true;
	virtual void DeclareResourceUsages(RenderPassBuilder* builder) {};

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
			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ResourceDescriptorIndicesRootSignatureIndex, 0, i, indices);
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

	virtual std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) { return nullptr; }
	virtual std::vector<ResourceIdentifier> GetSupportedKeys() { return {}; }

	std::unique_ptr<ResourceDescriptorIndexHelper> m_resourceDescriptorIndexHelper;
	std::shared_ptr<ResourceRegistryView> m_resourceRegistryView;
	friend class RenderPassBuilder;
};