#pragma once

#include <vector>

#include "Resource.h"
#include "RenderContext.h"
#include "ResourceStates.h"

struct PassParameters {
    std::vector<std::shared_ptr<Resource>> shaderResources;
    std::vector<std::shared_ptr<Resource>> renderTargets;
    std::shared_ptr<Resource> depthAttachment = nullptr;
};

class RenderPass {
public:
    virtual ~RenderPass() = default;

    virtual void Setup(RenderContext& context) = 0;
    virtual void Execute(RenderContext& context) = 0;
    virtual void Cleanup(RenderContext& context) = 0;

    // Declare resources this pass will read from and write to
    //virtual void DeclareResources() = 0;

    // Accessors for resource lists
    const std::vector<Resource*>& GetReadResources() const { return readResources; }
    const std::vector<Resource*>& GetWriteResources() const { return writeResources; }

protected:
    std::vector<Resource*> readResources;
    std::vector<Resource*> writeResources;
};