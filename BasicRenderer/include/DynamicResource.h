#pragma once

#pragma once

#include <memory>
#include <string>
#include "Resource.h"
#include "GloballyIndexedResource.h"

class DynamicResource : public Resource {
public:
    DynamicResource(std::shared_ptr<Resource> initialResource)
        : resource(std::move(initialResource)) {
        if (resource) {
            currentState = resource->GetState();
            name = resource->GetName();
        }
    }

    // Allow swapping the underlying resource dynamically
    void SetResource(std::shared_ptr<Resource> newResource) {
        if (!newResource) {
            throw std::runtime_error("Cannot set a null resource.");
        }

        resource = std::move(newResource);
        currentState = resource->GetState();
        name = resource->GetName();
    }

    std::shared_ptr<Resource> GetResource() const {
        return resource;
    }

    void Transition(ID3D12GraphicsCommandList* commandList,
        ResourceState prevState, ResourceState newState) override {
        if (resource) {
            resource->Transition(commandList, prevState, newState);
            SetState(newState); // Keep the wrapper's state in sync
        }
    }

protected:
    void OnSetName() override {
        if (resource) {
            resource->SetName(name);
        }
    }

private:
    std::shared_ptr<Resource> resource; // T actual resource
};

class DynamicGloballyIndexedResource : public GloballyIndexedResourceBase {
public:
    DynamicGloballyIndexedResource(std::shared_ptr<GloballyIndexedResource> initialResource)
        : m_resource(std::move(initialResource)) {
        if (m_resource) {
            currentState = m_resource->GetState();
            name = m_resource->GetName();
        }
    }

    // Allow swapping the underlying resource dynamically
    void SetResource(std::shared_ptr<GloballyIndexedResource> newResource) {
        if (!newResource) {
            throw std::runtime_error("Cannot set a null resource.");
        }
        m_resource = std::move(newResource);
        currentState = m_resource->GetState();
        name = m_resource->GetName();
    }

    std::shared_ptr<Resource> GetResource() const {
        return m_resource;
    }

    void Transition(ID3D12GraphicsCommandList* commandList,
        ResourceState prevState, ResourceState newState) override {
        if (m_resource) {
            m_resource->Transition(commandList, prevState, newState);
            SetState(newState); // Keep the wrapper's state in sync
        }
    }

protected:
    void OnSetName() override {
        if (m_resource) {
            m_resource->SetName(name);
        }
    }

private:
    std::shared_ptr<GloballyIndexedResource> m_resource; // T actual resource
};