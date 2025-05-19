#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Resources/Resource.h"
#include "Resources/GloballyIndexedResource.h"
#include "Render/RenderContext.h"

class ResourceGroup : public Resource {
public:
    ResourceGroup(const std::wstring& groupName) {
		name = groupName;
    }

	void AddResource(std::shared_ptr<Resource> resource) {
		auto id = resource->GetGlobalResourceID();
		if (!resourcesByID.contains(id)) {
			resourcesByID[resource->GetGlobalResourceID()] = resource;
			resources.push_back(resource);
		}
	}

	void RemoveResource(Resource* resource) {
		resources.erase(std::remove(resources.begin(), resources.end(), resourcesByID[resource->GetGlobalResourceID()]), resources.end());
		resourcesByID.erase(resource->GetGlobalResourceID());
	}

	void ClearResources() {
		resourcesByID.clear();
	}

	ID3D12Resource* GetAPIResource() const override {
		spdlog::error("ResourceGroup::GetAPIResource() should never be called, as it is not a single resource.");
		return nullptr;
	}

protected:

	BarrierGroups GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {

		std::vector<BarrierGroups> children;
		children.reserve(standardTransitionResources.size());

		size_t totalBufDescs   = 0,
			totalTexDescs   = 0,
			totalGlobDescs  = 0,
			totalBufGroups  = 0,
			totalTexGroups  = 0,
			totalGlobGroups = 0;

		for (auto& r : standardTransitionResources) {
			auto child = r->GetEnhancedBarrierGroup(
				range,
				prevAccessType, newAccessType,
				prevLayout,    newLayout,
				prevSyncState, newSyncState);

			totalBufDescs   += child.bufferBarrierDescs.size();
			totalTexDescs   += child.textureBarrierDescs.size();
			totalGlobDescs  += child.globalBarrierDescs.size();

			totalBufGroups  += child.bufferBarriers.size();
			totalTexGroups  += child.textureBarriers.size();
			totalGlobGroups += child.globalBarriers.size();

			children.push_back(std::move(child));
		}

		BarrierGroups merged;

		// prevent any reallocation
		merged.bufferBarrierDescs.reserve(totalBufDescs);
		merged.textureBarrierDescs.reserve(totalTexDescs);
		merged.globalBarrierDescs.reserve(totalGlobDescs);

		merged.bufferBarriers.reserve(totalBufGroups);
		merged.textureBarriers.reserve(totalTexGroups);
		merged.globalBarriers.reserve(totalGlobGroups);

		for (auto& child : children) {
			size_t bufDescOffset = merged.bufferBarrierDescs.size();
			merged.bufferBarrierDescs.insert(
				merged.bufferBarrierDescs.end(),
				child.bufferBarrierDescs.begin(),
				child.bufferBarrierDescs.end());

			for (auto const& grp : child.bufferBarriers) {
				D3D12_BARRIER_GROUP g = grp;
				// this was the index *within* the child's desc array
				ptrdiff_t idx = grp.pBufferBarriers
					- child.bufferBarrierDescs.data();
				// now point at merged + offset
				g.pBufferBarriers = merged.bufferBarrierDescs.data()
					+ (bufDescOffset + idx);
				merged.bufferBarriers.push_back(g);
			}

			size_t texDescOffset = merged.textureBarrierDescs.size();
			merged.textureBarrierDescs.insert(
				merged.textureBarrierDescs.end(),
				child.textureBarrierDescs.begin(),
				child.textureBarrierDescs.end());

			for (auto const& grp : child.textureBarriers) {
				D3D12_BARRIER_GROUP g = grp;
				ptrdiff_t idx = grp.pTextureBarriers
					- child.textureBarrierDescs.data();
				g.pTextureBarriers = merged.textureBarrierDescs.data()
					+ (texDescOffset + idx);
				merged.textureBarriers.push_back(g);
			}

			size_t globDescOffset = merged.globalBarrierDescs.size();
			merged.globalBarrierDescs.insert(
				merged.globalBarrierDescs.end(),
				child.globalBarrierDescs.begin(),
				child.globalBarrierDescs.end());

			for (auto const& grp : child.globalBarriers) {
				D3D12_BARRIER_GROUP g = grp;
				ptrdiff_t idx = grp.pGlobalBarriers
					- child.globalBarrierDescs.data();
				g.pGlobalBarriers = merged.globalBarrierDescs.data()
					+ (globDescOffset + idx);
				merged.globalBarriers.push_back(g);
			}
		}
		return merged;
	}

    std::unordered_map<uint64_t, std::shared_ptr<Resource>> resourcesByID;
	std::vector<std::shared_ptr<Resource>> resources;
	std::vector<std::shared_ptr<Resource>> standardTransitionResources;
    std::vector<D3D12_RESOURCE_BARRIER> m_transitions;
	
	// New barriers
	std::vector<D3D12_BARRIER_GROUP> m_bufferBarriers;
	std::vector<D3D12_BARRIER_GROUP> m_textureBarriers;
	std::vector<D3D12_BARRIER_GROUP> m_globalBarriers;

	BarrierGroups m_barrierGroups;

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
