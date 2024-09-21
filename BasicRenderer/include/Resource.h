#pragma once

#include <string>
#include <vector>

#include "ResourceStates.h"
//#include "RenderPass.h"

class RenderContext;

class Resource {
public:
    Resource() : currentState(ResourceState::Undefined) {}

    ResourceState GetState() const { return currentState; }

    const std::wstring& GetName() const { return name; }
	virtual void SetName(const std::wstring& name) { this->name = name; }

protected:
    virtual void Transition(RenderContext& context, ResourceState prevState, ResourceState newState) = 0;
    ResourceState currentState;
    std::wstring name;
private:
    friend class RenderGraph;
    friend class ResourceGroup;
};