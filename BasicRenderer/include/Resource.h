#pragma once

#include <string>
#include <vector>

#include "ResourceStates.h"
//#include "RenderPass.h"

class Resource {
public:
    Resource(const std::string& name) : name(name), currentState(ResourceState::Undefined) {}
    Resource() : currentState(ResourceState::Undefined) {}

    virtual void SetState(ResourceState state) { Transition(state); currentState = state; }
    ResourceState GetState() const { return currentState; }

    const std::string& GetName() const { return name; }

protected:
    virtual void Transition(ResourceState newState) {};
private:
    std::string name;
    ResourceState currentState;
};