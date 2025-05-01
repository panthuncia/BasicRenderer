#pragma once

#include <vector>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "Resources/Resource.h"
#include "Render/RenderContext.h"
#include "Resources/ResourceStates.h"
#include "Render/ResourceRequirements.h"

struct ComputePassParameters {
	std::vector<std::shared_ptr<Resource>> shaderResources;
	std::vector<std::shared_ptr<Resource>> constantBuffers;
	std::vector<std::shared_ptr<Resource>> unorderedAccessViews;

	std::vector<ResourceRequirement> resourceRequirements;
};

struct ComputePassReturn {
	std::vector<ID3D12CommandList*> commandLists;
	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;
};

class ComputePass {
public:
	virtual ~ComputePass() = default;

	virtual void Setup() = 0;
	virtual void Update() {};
	virtual ComputePassReturn Execute(RenderContext& context) = 0;
	virtual void Cleanup(RenderContext& context) = 0;

	void Invalidate() { invalidated = true; }
	bool IsInvalidated() const { return invalidated; }

protected:
	bool invalidated = true;
};