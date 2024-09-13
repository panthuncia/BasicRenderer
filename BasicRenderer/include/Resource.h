#pragma once

#include <string>
#include <vector>

#include "ResourceStates.h"
//#include "RenderPass.h"

class Resource {
public:
    Resource(const std::string& name) : name(name), currentState(ResourceState::Undefined) {}

    void SetState(ResourceState state) { currentState = state; }
    ResourceState GetState() const { return currentState; }

    const std::string& GetName() const { return name; }

private:
    std::string name;
    ResourceState currentState;
};