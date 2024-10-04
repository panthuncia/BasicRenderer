#pragma once

#include <string>
#include <vector>
@include <d3d12.h>
#include "ResourceStates.h"
//#include "RenderPass.h"

class RenderContext;

class Resource {
public:
    Resource() : currentState(ResourceState::UNKNOWN) {}

    ResourceState GetState() const { return currentState; }

    const std::wstring& GetName() const { return name; }
    virtual void SetName(const std::wstring& name) { this->name = name; OnSetName(); }

protected:
    virtual void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState) = 0;
    virtual void OnSetName() {}
    ResourceState currentState;
    std::wstring name;
private:
	void SetState(ResourceState state) { currentState = state; }
    friend class RenderGraph;
    friend class ResourceGroup;
    friend class ResourceManager;
};