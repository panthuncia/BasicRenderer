#pragma once

#pragma once

#include <memory>
#include <string>
#include "Resources/Resource.h"
#include "Resources/GloballyIndexedResource.h"

class DynamicResource : public Resource {
public:
    DynamicResource(std::shared_ptr<Resource> initialResource)
        : resource(std::move(initialResource)) {
        if (resource) {
            //currentState = resource->GetState();
            name = resource->GetName();
        }
    }

    // Allow swapping the underlying resource dynamically
    void SetResource(std::shared_ptr<Resource> newResource) {
        if (!newResource) {
            throw std::runtime_error("Cannot set a null resource.");
        }

        resource = std::move(newResource);
        //currentState = resource->GetState();
        name = resource->GetName();
    }

    std::shared_ptr<Resource> GetResource() const {
        return resource;
    }

    /*ResourceAccessType GetSubresourceAccessType(unsigned int subresourceIndex) const override { return resource->GetSubresourceAccessType(subresourceIndex); }
    ResourceLayout GetSubresourceLayout(unsigned int subresourceIndex) const override { return resource->GetSubresourceLayout(subresourceIndex); }
    ResourceSyncState GetSubresourceSyncState(unsigned int subresourceIndex) const override { return resource->GetSubresourceSyncState(subresourceIndex); }*/

    virtual BarrierGroups GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
        if (resource) {
            //m_currentAccessType = newAccessType;
            //m_currentLayout = newLayout;
            //m_prevSyncState = newSyncState;
            return resource->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
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
            //currentState = m_resource->GetState();
            name = m_resource->GetName();
        }
    }

    // Allow swapping the underlying resource dynamically
    void SetResource(std::shared_ptr<GloballyIndexedResource> newResource) {
        if (!newResource) {
            throw std::runtime_error("Cannot set a null resource.");
        }
        m_resource = std::move(newResource);
        //currentState = m_resource->GetState();
        name = m_resource->GetName();
    }

    std::shared_ptr<GloballyIndexedResource> GetResource() const {
        return m_resource;
    }

    virtual BarrierGroups GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
		if (m_resource) {
			//SetState(newState); // Keep the wrapper's state in sync
			return m_resource->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
		}
        return {};
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