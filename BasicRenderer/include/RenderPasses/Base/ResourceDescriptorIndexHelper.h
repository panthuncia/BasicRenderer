#pragma once

#include <unordered_map>
#include <memory>

#include "Resources/DynamicResource.h"
#include "Render/ResourceRegistry.h"

class ResourceIndexOrDynamicResource {
public:
	ResourceIndexOrDynamicResource() = default;
	ResourceIndexOrDynamicResource(bool isDynamic, unsigned int index)
		: isDynamic(isDynamic), index(index) {
	}
	ResourceIndexOrDynamicResource(std::shared_ptr<DynamicGloballyIndexedResource> dynamicResource)
		: isDynamic(true), dynamicResource(dynamicResource) {
	}
	ResourceIndexOrDynamicResource(const ResourceIndexOrDynamicResource& other) noexcept
		: isDynamic(other.isDynamic) {
		if (isDynamic) {
			dynamicResource = other.dynamicResource;
		}
		else {
			index = other.index;
		}
	}
	~ResourceIndexOrDynamicResource() = default;
	bool isDynamic = false; // Indicates if this is a dynamic resource or a static index
	unsigned int index; // Index in the descriptor heap
	std::shared_ptr<DynamicGloballyIndexedResource> dynamicResource; // Pointer to a dynamic resource if applicable
};

enum class DescriptorType {
	SRV,
	UAV
};

struct DescriptorAccessor {
	DescriptorType type; // Type of the descriptor (SRV or UAV)
	bool hasSRVViewType = false; // Indicates if a specific SRVViewType is set
	SRVViewType SRVType; // Type of the SRV
	unsigned int mip; // Mip level
	unsigned int slice; // Slice index
};

struct ResourceAndAccessor {
	ResourceIndexOrDynamicResource resource;
	DescriptorAccessor accessor; // Accessor for the descriptor
};

class ResourceDescriptorIndexHelper {
public:
	ResourceDescriptorIndexHelper(std::shared_ptr<ResourceRegistryView> registryView) : m_resourceRegistryView(registryView) {

	}
	void RegisterSRV(SRVViewType type, ResourceIdentifier id, unsigned int mip, unsigned int slice = 0) {
		DescriptorAccessor accessor;
		accessor.type = DescriptorType::SRV;
		accessor.hasSRVViewType = true;
		accessor.SRVType = type;
		accessor.mip = mip;
		accessor.slice = slice;
		auto resource = m_resourceRegistryView->Request<Resource>(id);
		auto resourceIndexOrDynamic = GetResourceIndexOrDynamicResource(id, resource, accessor);
		m_resourceMap[id.hash] = ResourceAndAccessor{ resourceIndexOrDynamic, accessor };
	}
	void RegisterSRV(ResourceIdentifier id, unsigned int mip, unsigned int slice = 0) {
		DescriptorAccessor accessor;
		accessor.type = DescriptorType::SRV;
		accessor.hasSRVViewType = false;
		accessor.mip = mip;
		accessor.slice = slice;
		auto resource = m_resourceRegistryView->Request<Resource>(id);
		auto resourceIndexOrDynamic = GetResourceIndexOrDynamicResource(id, resource, accessor);
		m_resourceMap[id.hash] = ResourceAndAccessor{ resourceIndexOrDynamic, accessor };
	}
	void RegisterUAV(ResourceIdentifier id, unsigned int mip, unsigned int slice = 0) {
		DescriptorAccessor accessor;
		accessor.type = DescriptorType::UAV;
		accessor.mip = mip;
		accessor.slice = slice;
		auto resource = m_resourceRegistryView->Request<Resource>(id);
		auto resourceIndexOrDynamic = GetResourceIndexOrDynamicResource(id, resource, accessor);
		m_resourceMap[id.hash] = ResourceAndAccessor{ resourceIndexOrDynamic, accessor };
	}
	unsigned int GetResourceDescriptorIndex(size_t hash, bool allowFail = true, const std::string* name = nullptr) const {
		auto it = m_resourceMap.find(hash);
		if (it == m_resourceMap.end()) {
			if (allowFail) {
				return std::numeric_limits<unsigned int>().max(); // Return max value if the resource is not found and allowFail is true
			}
			std::string resourceName = name ? *name : "Unknown";
			throw std::runtime_error("Resource "+ resourceName +" not found!");
		}
		const auto& resourceAndAccessor = it->second;
		if (resourceAndAccessor.resource.isDynamic) {
			return AccessDynamicGloballyIndexedResource(resourceAndAccessor.resource.dynamicResource, resourceAndAccessor.accessor);
		}
		else {
			return resourceAndAccessor.resource.index;
		}
	}
	unsigned int GetResourceDescriptorIndex(ResourceIdentifier& id, bool allowFail = true) {
		return GetResourceDescriptorIndex(id.hash, allowFail);
	}
private:
	std::unordered_map<size_t, ResourceAndAccessor> m_resourceMap; // Maps resource identifiers to descriptor indices

	unsigned int AccessGloballyIndexedResource(const std::shared_ptr<GloballyIndexedResource> resource, const DescriptorAccessor& accessor) const {
		switch (accessor.type) {
		case DescriptorType::SRV:
			if (accessor.hasSRVViewType) {
				return resource->GetSRVInfo(accessor.SRVType, accessor.mip, accessor.slice).slot.index;
			}
			else {
				return resource->GetSRVInfo(accessor.mip, accessor.slice).slot.index;
			}
		case DescriptorType::UAV:
			return resource->GetUAVShaderVisibleInfo(accessor.mip, accessor.slice).slot.index;
		default:
			throw std::runtime_error("Unsupported descriptor type");
		}
	}

	unsigned int AccessDynamicGloballyIndexedResource(const std::shared_ptr<DynamicGloballyIndexedResource> resource, const DescriptorAccessor& accessor) const {
		return AccessGloballyIndexedResource(resource->GetResource(), accessor);
	}

	ResourceIndexOrDynamicResource GetResourceIndexOrDynamicResource(const ResourceIdentifier& id, const std::shared_ptr<Resource> resource, const DescriptorAccessor& accessor) const {
		if (auto dynamicResource = std::dynamic_pointer_cast<DynamicGloballyIndexedResource>(resource)) {
			return ResourceIndexOrDynamicResource(dynamicResource);
		}
		else {
			// Otherwise, check if it's a globally indexed resource
			auto globallyIndexedResource = std::dynamic_pointer_cast<GloballyIndexedResource>(resource);
			if (!globallyIndexedResource) {
				throw std::runtime_error("Resource is not a GloballyIndexedResource or DynamicGloballyIndexedResource");
			}
			return ResourceIndexOrDynamicResource(false, AccessGloballyIndexedResource(globallyIndexedResource, accessor));
		}
	}

	std::shared_ptr<ResourceRegistryView> m_resourceRegistryView;
};