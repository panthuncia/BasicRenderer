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

struct RenderPassParameters {
    std::vector<RangeSpec> shaderResources;
    std::vector<RangeSpec> renderTargets;
    std::vector<RangeSpec> depthReadResources;
	std::vector<RangeSpec> depthReadWriteResources;
	std::vector<RangeSpec> constantBuffers;
	std::vector<RangeSpec> unorderedAccessViews;
	std::vector<RangeSpec> copyTargets;
	std::vector<RangeSpec> copySources;
	std::vector<RangeSpec> indirectArgumentBuffers;

	std::vector<ResourceRequirement> resourceRequirements;
	bool isGeometryPass = false;
};

class RenderPass {
public:
    virtual ~RenderPass() = default;

    virtual void Setup() = 0;
	virtual void Update() {};
    virtual PassReturn Execute(RenderContext& context) = 0;
    virtual void Cleanup(RenderContext& context) = 0;

	void Invalidate() { invalidated = true; }
	bool IsInvalidated() const { return invalidated; }

protected:
	bool invalidated = true;
};