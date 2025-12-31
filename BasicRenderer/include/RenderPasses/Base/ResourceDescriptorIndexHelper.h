#pragma once

#include <unordered_map>
#include <memory>

#include "Resources/DynamicResource.h"
#include "Render/ResourceRegistry.h"

class ResourceIndexOrDynamicResource {
public:
	bool isDynamic = false;
	unsigned int index = 0;
	ResourceRegistry::ResourceHandle handle{}; // for dynamic case

	static ResourceIndexOrDynamicResource Static(unsigned idx) {
		ResourceIndexOrDynamicResource r; r.isDynamic = false; r.index = idx; return r;
	}
	static ResourceIndexOrDynamicResource Dynamic(ResourceRegistry::ResourceHandle h) {
		ResourceIndexOrDynamicResource r; r.isDynamic = true; r.handle = h; return r;
	}
};


enum class DescriptorType {
	SRV,
	UAV,
	CBV
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

		auto h = m_resourceRegistryView->RequestHandle(id);
		Resource* res = m_resourceRegistryView->Resolve<Resource>(h);

		auto entry = GetResourceIndexOrDynamicResource(h, res, accessor);
		m_resourceMap[id.hash] = ResourceAndAccessor{ entry, accessor };
	}
	void RegisterSRV(ResourceIdentifier id, unsigned int mip, unsigned int slice = 0) {
		DescriptorAccessor accessor;
		accessor.type = DescriptorType::SRV;
		accessor.hasSRVViewType = false;
		accessor.mip = mip;
		accessor.slice = slice;
		auto h = m_resourceRegistryView->RequestHandle(id);
		Resource* res = m_resourceRegistryView->Resolve<Resource>(h);

		auto entry = GetResourceIndexOrDynamicResource(h, res, accessor);
		m_resourceMap[id.hash] = ResourceAndAccessor{ entry, accessor };
	}
	void RegisterUAV(ResourceIdentifier id, unsigned int mip, unsigned int slice = 0) {
		DescriptorAccessor accessor;
		accessor.type = DescriptorType::UAV;
		accessor.mip = mip;
		accessor.slice = slice;
		auto h = m_resourceRegistryView->RequestHandle(id);
		Resource* res = m_resourceRegistryView->Resolve<Resource>(h);

		auto entry = GetResourceIndexOrDynamicResource(h, res, accessor);
		m_resourceMap[id.hash] = ResourceAndAccessor{ entry, accessor };
	}
	void RegisterCBV(ResourceIdentifier id) {
		DescriptorAccessor accessor;
		accessor.type = DescriptorType::CBV;
		auto h = m_resourceRegistryView->RequestHandle(id);
		Resource* res = m_resourceRegistryView->Resolve<Resource>(h);

		auto entry = GetResourceIndexOrDynamicResource(h, res, accessor);
		m_resourceMap[id.hash] = ResourceAndAccessor{ entry, accessor };
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
			return AccessDynamicGloballyIndexedResource(resourceAndAccessor.resource.handle, resourceAndAccessor.accessor);
		}
		else {
			return resourceAndAccessor.resource.index;
		}
	}
	unsigned int GetResourceDescriptorIndex(const ResourceIdentifier& id, bool allowFail = true) {
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
		case DescriptorType::CBV:
			return resource->GetCBVInfo().slot.index;
		default:
			throw std::runtime_error("Unsupported descriptor type");
		}
	}

	unsigned int AccessGloballyIndexedResource(
		const GloballyIndexedResource& resource,
		const DescriptorAccessor& accessor) const
	{
		switch (accessor.type) {
		case DescriptorType::SRV:
			if (accessor.hasSRVViewType) {
				return resource.GetSRVInfo(accessor.SRVType, accessor.mip, accessor.slice).slot.index;
			}
			else {
				return resource.GetSRVInfo(accessor.mip, accessor.slice).slot.index;
			}

		case DescriptorType::UAV:
			return resource.GetUAVShaderVisibleInfo(accessor.mip, accessor.slice).slot.index;

		case DescriptorType::CBV:
			return resource.GetCBVInfo().slot.index;

		default:
			throw std::runtime_error("Unsupported descriptor type");
		}
	}

	template<class T>
	static T* PtrFrom(T* p) noexcept { return p; }

	template<class T>
	static T* PtrFrom(const std::shared_ptr<T>& p) noexcept { return p.get(); }

	unsigned int AccessDynamicGloballyIndexedResource(
		const ResourceRegistry::ResourceHandle& h,
		const DescriptorAccessor& accessor) const
	{
		// Prefer a Resolve<T>() on the view; otherwise use Resolve() + dynamic_cast.
		Resource* base = m_resourceRegistryView->Resolve<Resource>(h);
		if (!base) {
			throw std::runtime_error("Dynamic resource handle no longer resolves");
		}

		auto* dyn = dynamic_cast<DynamicGloballyIndexedResource*>(base);
		if (!dyn) {
			throw std::runtime_error("Handle does not resolve to DynamicGloballyIndexedResource");
		}

		// backing may change frame-to-frame
		auto backing = dyn->GetResource();
		auto* gi = PtrFrom(backing);
		if (!gi) {
			throw std::runtime_error("Dynamic resource has null backing resource");
		}

		return AccessGloballyIndexedResource(*gi, accessor);
	}


	ResourceIndexOrDynamicResource GetResourceIndexOrDynamicResource(
		const ResourceRegistry::ResourceHandle& h,
		Resource* resource,
		const DescriptorAccessor& accessor) const
	{
		if (!resource) {
			throw std::runtime_error("Resource is null");
		}

		if (dynamic_cast<DynamicGloballyIndexedResource*>(resource)) {
			// Store the handle so we can re-resolve later.
			return ResourceIndexOrDynamicResource::Dynamic(h);
		}

		if (auto* gi = dynamic_cast<GloballyIndexedResource*>(resource)) {
			const unsigned idx = AccessGloballyIndexedResource(*gi, accessor);
			return ResourceIndexOrDynamicResource::Static(idx);
		}

		throw std::runtime_error(
			"Resource is not a GloballyIndexedResource or DynamicGloballyIndexedResource");
	}


	std::shared_ptr<ResourceRegistryView> m_resourceRegistryView;
};