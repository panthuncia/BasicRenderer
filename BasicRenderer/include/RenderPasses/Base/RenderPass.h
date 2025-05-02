#pragma once

#include <vector>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "Resources/Resource.h"
#include "Render/RenderContext.h"
#include "Resources/ResourceStates.h"
#include "Render/ResourceRequirements.h"
#include "RenderPasses/Base/PassReturn.h"

struct RenderPassParameters {
    std::vector<std::shared_ptr<Resource>> shaderResources;
    std::vector<std::shared_ptr<Resource>> renderTargets;
    std::vector<std::shared_ptr<Resource>> depthReadResources;
	std::vector<std::shared_ptr<Resource>> depthReadWriteResources;
	std::vector<std::shared_ptr<Resource>> constantBuffers;
	std::vector<std::shared_ptr<Resource>> unorderedAccessViews;
	std::vector<std::shared_ptr<Resource>> copyTargets;
	std::vector<std::shared_ptr<Resource>> copySources;
	std::vector<std::shared_ptr<Resource>> indirectArgumentBuffers;

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