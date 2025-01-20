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

    std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(
        ResourceState prevState, ResourceState newState) override {
        if (resource) {
            SetState(newState); // Keep the wrapper's state in sync
            return resource->GetTransitions(prevState, newState);
        }
    }

    virtual BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
        if (resource) {
            SetState(newState); // Keep the wrapper's state in sync
            return resource->GetEnhancedBarrierGroup(prevState, newState, prevSyncState, newSyncState);
        }
    }

	ID3D12Resource* GetAPIResource() const override {
		if (resource) {
			return resource->GetAPIResource();
		}
		return nullptr;
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

    std::shared_ptr<GloballyIndexedResource> GetResource() const {
        return m_resource;
    }

    std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(
        ResourceState prevState, ResourceState newState) override {
        if (m_resource) {
            SetState(newState); // Keep the wrapper's state in sync
            return m_resource->GetTransitions(prevState, newState);
        }
    }

    virtual BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
		if (m_resource) {
			SetState(newState); // Keep the wrapper's state in sync
			return m_resource->GetEnhancedBarrierGroup(prevState, newState, prevSyncState, newSyncState);
		}
    }

    ID3D12Resource* GetAPIResource() const override {
        if (m_resource) {
            return m_resource->GetAPIResource();
        }
        return nullptr;
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