#pragma once

#include <string>
#include <vector>
#include <directx/d3d12.h>
#include "ResourceStates.h"

class RenderContext;

class Resource {
public:
    Resource() : currentState(ResourceState::UNKNOWN) {}

    virtual ResourceState GetState() const { return currentState; }

    const std::wstring& GetName() const { return name; }
    virtual void SetName(const std::wstring& name) { this->name = name; OnSetName(); }
	virtual ID3D12Resource* GetAPIResource() const = 0;

protected:
    virtual std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState prevState, ResourceState newState) = 0;
    virtual void OnSetName() {}
    ResourceState currentState;
    std::wstring name;
private:

	void SetState(ResourceState state) { currentState = state; }
    friend class RenderGraph;
    friend class ResourceGroup;
    friend class ResourceManager;
    friend class DynamicResource;
    friend class DynamicGloballyIndexedResource;
};