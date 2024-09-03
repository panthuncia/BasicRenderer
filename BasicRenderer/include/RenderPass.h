#pragma once
class RenderPass {
public:
    virtual ~RenderPass() = default;

    virtual void Setup(RenderContext& context) = 0;
    virtual void Execute(RenderContext& context) = 0;
    virtual void Cleanup(RenderContext& context) = 0;
};