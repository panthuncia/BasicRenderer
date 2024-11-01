#pragma once

#include <vector>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "Resource.h"
#include "RenderContext.h"
#include "ResourceStates.h"

struct PassParameters {
    std::vector<std::shared_ptr<Resource>> shaderResources;
    std::vector<std::shared_ptr<Resource>> renderTargets;
    std::vector<std::shared_ptr<Resource>> depthTextures;
};

class RenderPass {
public:
    virtual ~RenderPass() = default;

    virtual void Setup() = 0;
    virtual std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) = 0;
    virtual void Cleanup(RenderContext& context) = 0;

	void Invalidate() { invalidated = true; }
	bool IsInvalidated() const { return invalidated; }

protected:
	bool invalidated = true;
};