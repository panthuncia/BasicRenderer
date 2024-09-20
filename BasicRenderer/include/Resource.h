#pragma once

#include <string>
#include <vector>

#include "ResourceStates.h"
//#include "RenderPass.h"

class RenderContext;

class Resource {
public:
    Resource(const std::string& name) : name(name), currentState(ResourceState::Undefined) {}
    Resource() : currentState(ResourceState::Undefined) {}

    ResourceState GetState() const { return currentState; }

    const std::string& GetName() const { return name; }
	void SetName(const std::string& name) { this->name = name; }

protected:
    virtual void Transition(RenderContext& context, ResourceState prevState, ResourceState newState) = 0;
    ResourceState currentState;
private:
    std::string name;
    friend class RenderGraph;
    friend class ResourceGroup;
};