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

struct ComputePassParameters {
	std::vector<ResourceAndRange> shaderResources;
	std::vector<ResourceAndRange> constantBuffers;
	std::vector<ResourceAndRange> unorderedAccessViews;
	std::vector<ResourceAndRange> indirectArgumentBuffers;
	std::vector<ResourceAndRange> legacyInteropResources;

	std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher> identifierSet;
	std::vector<ResourceRequirement> resourceRequirements;
};

class ComputePassBuilder;

class ComputePass {
public:
	virtual ~ComputePass() = default;

	virtual void Setup(const ResourceRegistryView& resourceRegistryView) = 0;
	virtual void RegisterCommandLists(std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> commandLists) {};

	virtual void Update() {};
	virtual PassReturn Execute(RenderContext& context) = 0;
	virtual void Cleanup(RenderContext& context) = 0;

	void Invalidate() { invalidated = true; }
	bool IsInvalidated() const { return invalidated; }

protected:
	bool invalidated = true;
	virtual void DeclareResourceUsages(ComputePassBuilder* builder) {};

	friend class ComputePassBuilder;
};