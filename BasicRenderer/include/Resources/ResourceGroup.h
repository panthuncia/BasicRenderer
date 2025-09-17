#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <rhi.h>

#include "Resources/Resource.h"
#include "Resources/GloballyIndexedResource.h"
#include "Render/RenderContext.h"

class ResourceGroup : public Resource {
public:
    ResourceGroup(const std::wstring& groupName) {
		name = groupName;
    }

	const std::vector<std::shared_ptr<Resource>>& GetChildren() {
		return resources;
	}

	void AddResource(std::shared_ptr<Resource> resource) {
		auto id = resource->GetGlobalResourceID();
		if (!resourcesByID.contains(id)) {
			resourcesByID[resource->GetGlobalResourceID()] = resource;
			resources.push_back(resource);
		}
	}

	void RemoveResource(Resource* resource) {
		const auto id = resource->GetGlobalResourceID();
		auto it = resourcesByID.find(id);
		if (it != resourcesByID.end()) {
			const auto& sp = it->second;
			resources.erase(std::remove(resources.begin(), resources.end(), sp), resources.end());
			resourcesByID.erase(it);
		}
	}

	void ClearResources() {
		resources.clear();
		resourcesByID.clear();
		standardTransitionResources.clear();
	}

	rhi::Resource GetAPIResource() override {
		spdlog::error("ResourceGroup::GetAPIResource() should never be called, as it is not a single resource.");
		throw std::runtime_error("ResourceGroup::GetAPIResource() should never be called, as it is not a single resource.");
	}

protected:

	rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {

		m_childBatches.clear();
		m_childBatches.reserve(standardTransitionResources.size());

		size_t totalTex = 0, totalBuf = 0, totalGlob = 0;

		for (auto& r : standardTransitionResources) {
			rhi::BarrierBatch child = r->GetEnhancedBarrierGroup(
				range, prevAccessType, newAccessType,
				prevLayout, newLayout, prevSyncState, newSyncState);

			totalTex += child.textures.size;
			totalBuf += child.buffers.size;
			totalGlob += child.globals.size;
			m_childBatches.push_back(child);
		}

		m_texBarriers.clear();
		m_bufBarriers.clear();
		m_globBarriers.clear();

		m_texBarriers.reserve(totalTex);
		m_bufBarriers.reserve(totalBuf);
		m_globBarriers.reserve(totalGlob);

		for (const auto& c : m_childBatches) {
			m_texBarriers.insert(
				m_texBarriers.end(),
				c.textures.begin(),
				c.textures.end());

			m_bufBarriers.insert(
				m_bufBarriers.end(),
				c.buffers.begin(),
				c.buffers.end());

			m_globBarriers.insert(
				m_globBarriers.end(),
				c.globals.begin(),
				c.globals.end());
		}

		// Return Span views into the owned vectors
		return rhi::BarrierBatch{
			{ m_texBarriers.data(), static_cast<uint32_t>(m_texBarriers.size()) },
			{ m_bufBarriers.data(), static_cast<uint32_t>(m_bufBarriers.size()) },
			{ m_globBarriers.data(), static_cast<uint32_t>(m_globBarriers.size()) }
		};
	}

    std::unordered_map<uint64_t, std::shared_ptr<Resource>> resourcesByID;
	std::vector<std::shared_ptr<Resource>> resources;
	std::vector<std::shared_ptr<Resource>> standardTransitionResources;
    std::vector<D3D12_RESOURCE_BARRIER> m_transitions;
	
	// New barriers
	std::vector<rhi::TextureBarrier> m_texBarriers;
	std::vector<rhi::BufferBarrier>  m_bufBarriers;
	std::vector<rhi::GlobalBarrier>  m_globBarriers;

	std::vector<rhi::BarrierBatch>   m_childBatches;

private:

	void InitializeForGraph() {
		standardTransitionResources.clear();
		for (auto& resource : resources) {
			standardTransitionResources.push_back(resource);
		}
	}

	std::vector<uint64_t> GetChildIDs() {
		std::vector<uint64_t> children;
		for (auto& resource : resources) {
			auto resourceGroup = std::dynamic_pointer_cast<ResourceGroup>(resource);
			if (resourceGroup) {
				auto grandchildren = resourceGroup->GetChildIDs();
				for (auto grandchild : grandchildren) {
					children.push_back(grandchild);
				}
			}
			children.push_back(resource->GetGlobalResourceID());
		}
		return children;
	}

	void MarkResourceAsNonStandard(std::shared_ptr<Resource> resource) {
		auto it = std::find(standardTransitionResources.begin(), standardTransitionResources.end(), resource);
		if (it != standardTransitionResources.end()) {
			standardTransitionResources.erase(it);
		}
	}

	friend class RenderGraph;
};
