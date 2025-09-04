#pragma once

#pragma once

#include <memory>
#include <string>
#include <rhi.h>
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

    virtual rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {
        if (resource) {
            return resource->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
        }
    }

    rhi::Resource GetAPIResource() const override {
		if (resource) {
			return resource->GetAPIResource();
		}
        return {};
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

    virtual rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {
		if (m_resource) {
			//SetState(newState); // Keep the wrapper's state in sync
			return m_resource->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
		}
        return {};
    }

    rhi::Resource GetAPIResource() const override {
        if (m_resource) {
            return m_resource->GetAPIResource();
        }
        return {};
    }

protected:
    void OnSetName() override {
        if (m_resource) {
            m_resource->SetName(name);
        }
    }

private:
    std::shared_ptr<GloballyIndexedResource> m_resource; // actual resource
};