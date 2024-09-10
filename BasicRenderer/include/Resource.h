#pragma once
enum class ResourceState {
    Undefined,
    Common,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderResource,
    CopySource,
    CopyDest,
    // Other states as needed
};

class Resource {
public:
    Resource(const std::string& name) : name(name), currentState(ResourceState::Undefined) {}

    void SetState(ResourceState state) { currentState = state; }
    ResourceState GetState() const { return currentState; }

    // Resource usage declaration
    void AddReadPass(RenderPass* pass) { readPasses.push_back(pass); }
    void AddWritePass(RenderPass* pass) { writePasses.push_back(pass); }

    const std::string& GetName() const { return name; }

private:
    std::string name;
    ResourceState currentState;
    std::vector<RenderPass*> readPasses;
    std::vector<RenderPass*> writePasses;
};