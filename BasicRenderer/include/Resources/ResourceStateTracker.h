#pragma once

#include <memory>
#include <vector>
#include <optional>
#include <algorithm>

#include "Resources/ResourceStates.h"

class Resource;

struct ResourceState {
    ResourceAccessType access;
    ResourceLayout     layout;
    ResourceSyncState  sync;
    bool operator==(ResourceState const& o) const {
        return access == o.access
            && layout == o.layout;
			//&& sync == o.sync; // Sync is not important for equality
    };
};

enum class BoundType {
    Exact,  // == value
    From,   // >= value
    UpTo,   // <= value
    All     // everything
};

struct Bound {
    BoundType type;
    uint32_t  value;  // only for Exact, From, UpTo

    bool operator==(Bound const& o) const noexcept {
        return type  == o.type
            && value == o.value;
    }
    bool operator!=(Bound const& o) const noexcept {
        return !(*this == o);
    }
};

struct RangeSpec {
    //std::shared_ptr<Resource> resource;
    Bound mipLower   = { BoundType::All, 0 };
    Bound mipUpper   = { BoundType::All, 0 };
    Bound sliceLower = { BoundType::All, 0 };
    Bound sliceUpper = { BoundType::All, 0 };
};

struct SubresourceRange {
	uint32_t firstMip;
	uint32_t mipCount;
	uint32_t firstSlice;
	uint32_t sliceCount;
	bool isEmpty() const {
		return (mipCount == 0) || (sliceCount == 0);
	}
};

SubresourceRange ResolveRangeSpec(RangeSpec spec,
    uint32_t totalMips,
    uint32_t totalSlices);

struct ResourceTransition {
    ResourceTransition() = default;
    ResourceTransition(Resource* pResource, RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState)
        : range(range), pResource(pResource), prevAccessType(prevAccessType), newAccessType(newAccessType), prevLayout(prevLayout), newLayout(newLayout), prevSyncState(prevSyncState), newSyncState(newSyncState) {
    }
    Resource* pResource;
    RangeSpec range;
    ResourceAccessType prevAccessType = ResourceAccessType::NONE;
    ResourceAccessType newAccessType = ResourceAccessType::NONE;
    ResourceLayout prevLayout = ResourceLayout::LAYOUT_COMMON;
    ResourceLayout newLayout = ResourceLayout::LAYOUT_COMMON;
    ResourceSyncState prevSyncState = ResourceSyncState::NONE;
    ResourceSyncState newSyncState = ResourceSyncState::NONE;
};

struct Segment {
    RangeSpec     rangeSpec;
    ResourceState state;
};

class SymbolicTracker {
    std::vector<Segment> _segs;
public:
    SymbolicTracker() {
		RangeSpec whole;
		//whole.resource = nullptr;
		whole.mipLower = { BoundType::All, 0 };
		whole.mipUpper = { BoundType::All, 0 };
		whole.sliceLower = { BoundType::All, 0 };
		whole.sliceUpper = { BoundType::All, 0 };
		_segs.push_back({ whole, ResourceState{ ResourceAccessType::COMMON, ResourceLayout::LAYOUT_COMMON, ResourceSyncState::ALL } });
    }
    SymbolicTracker(RangeSpec whole, ResourceState init) {
        _segs.push_back({ whole, init });
    }

    // apply a new requirement and emit transitions
    void Apply(RangeSpec want,
        Resource* pRes,
        ResourceState newState,
        std::vector<ResourceTransition>& out);

    bool WouldModify(RangeSpec want, ResourceState newState) const;

    const std::vector<Segment>& GetSegments() const noexcept;
};