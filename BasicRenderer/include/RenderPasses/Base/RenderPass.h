#pragma once

#include <vector>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "Resources/Resource.h"
#include "Render/RenderContext.h"
#include "Resources/ResourceStates.h"
#include "Render/ResourceRequirements.h"
#include "RenderPasses/Base/PassReturn.h"
#include "Resources/ResourceStateTracker.h"

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

	std::vector<ResourceRequirement> resourceRequirements;
	bool isGeometryPass = false;
};

class RenderPassBuilder;

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
	virtual void DeclareResourceUsages(RenderPassBuilder* builder) {};
	
	friend class RenderPassBuilder;
};