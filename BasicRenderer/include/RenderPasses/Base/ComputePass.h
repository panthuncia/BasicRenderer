#pragma once

#include <vector>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "Resources/Resource.h"
#include "Render/RenderContext.h"
#include "Resources/ResourceStates.h"
#include "Render/ResourceRequirements.h"
#include "RenderPasses/Base/PassReturn.h"
#include "Resources/SubresourceView.h"

struct ComputePassParameters {
	std::vector<ResourceAndRange> shaderResources;
	std::vector<ResourceAndRange> constantBuffers;
	std::vector<ResourceAndRange> unorderedAccessViews;
	std::vector<ResourceAndRange> indirectArgumentBuffers;

	std::vector<ResourceRequirement> resourceRequirements;
};

class ComputePass {
public:
	virtual ~ComputePass() = default;

	virtual void Setup() = 0;
	virtual void Update() {};
	virtual PassReturn Execute(RenderContext& context) = 0;
	virtual void Cleanup(RenderContext& context) = 0;

	void Invalidate() { invalidated = true; }
	bool IsInvalidated() const { return invalidated; }

protected:
	bool invalidated = true;
};