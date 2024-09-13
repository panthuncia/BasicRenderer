#pragma once

#include <string>
#include <vector>

#include "ResourceStates.h"
//#include "RenderPass.h"

class Resource {
public:
    Resource(const std::string& name) : name(name), currentState(ResourceState::Undefined) {}

    void SetState(ResourceState state) { Transition(state); currentState = state; }
    ResourceState GetState() const { return currentState; }

    const std::string& GetName() const { return name; }

private:
	// Pure virtual function- subclasses must implement this
    virtual void Transition(ResourceState newState) = 0;
    std::string name;
    ResourceState currentState;
};