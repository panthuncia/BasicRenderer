#pragma once

#include <vector>
#include "RenderPass.h"

class RenderGraph {
public:
    void AddPass(RenderPass* renderPass) {
        passes.push_back(renderPass);
    }

    void Execute(RenderContext& context) {
        for (auto& pass : passes) {
            pass->Setup(context);
            pass->Execute(context);
            pass->Cleanup(context);
        }
    }

private:
    std::vector<RenderPass*> passes;
};