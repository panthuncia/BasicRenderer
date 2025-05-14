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
    bool operator==(ResourceState const& o) const = default;
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
    uint32_t totalSlices)
{
    // determine firstMip
    uint32_t firstMip = 0;
    switch (spec.mipLower.type) {
    case BoundType::Exact: firstMip = spec.mipLower.value; break;
    case BoundType::From:  firstMip = spec.mipLower.value; break;
    case BoundType::UpTo:  firstMip = 0;                    break;
    case BoundType::All:   firstMip = 0;                    break;
    }

    // determine lastMip (inclusive)
    uint32_t lastMip;
    switch (spec.mipUpper.type) {
    case BoundType::Exact: lastMip = spec.mipUpper.value;      break;
    case BoundType::UpTo:  lastMip = spec.mipUpper.value;      break;
    case BoundType::From:  lastMip = totalMips - 1;            break;
    case BoundType::All:   lastMip = totalMips - 1;            break;
    }

    // clamp to [0..totalMips-1]
    firstMip = std::min(firstMip,  totalMips);
    lastMip  = std::min(lastMip,   totalMips - 1);

    uint32_t numMips = (lastMip >= firstMip)
        ? (lastMip - firstMip + 1)
        : 0;

    // determine firstSlice
    uint32_t firstSlice = 0;
    switch (spec.sliceLower.type) {
    case BoundType::Exact: firstSlice = spec.sliceLower.value; break;
    case BoundType::From:  firstSlice = spec.sliceLower.value; break;
    case BoundType::UpTo:  firstSlice = 0;                     break;
    case BoundType::All:   firstSlice = 0;                     break;
    }

    // determine lastSlice (inclusive)
    uint32_t lastSlice;
    switch (spec.sliceUpper.type) {
    case BoundType::Exact: lastSlice = spec.sliceUpper.value;      break;
    case BoundType::UpTo:  lastSlice = spec.sliceUpper.value;      break;
    case BoundType::From:  lastSlice = totalSlices - 1;            break;
    case BoundType::All:   lastSlice = totalSlices - 1;            break;
    }

    // clamp to [0..totalSlices-1]
    firstSlice = std::min(firstSlice, totalSlices);
    lastSlice  = std::min(lastSlice,  totalSlices - 1);

    uint32_t numSlices = (lastSlice >= firstSlice)
        ? (lastSlice - firstSlice + 1)
        : 0;

    return { firstMip, numMips, firstSlice, numSlices };
}

struct ResourceTransition {
    ResourceTransition() = default;
    ResourceTransition(std::shared_ptr<Resource> pResource, RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState)
        : range(range), pResource(pResource), prevAccessType(prevAccessType), newAccessType(newAccessType), prevLayout(prevLayout), newLayout(newLayout), prevSyncState(prevSyncState), newSyncState(newSyncState) {
    }
    std::shared_ptr<Resource> pResource;
    RangeSpec range;
    ResourceAccessType prevAccessType = ResourceAccessType::NONE;
    ResourceAccessType newAccessType = ResourceAccessType::NONE;
    ResourceLayout prevLayout = ResourceLayout::LAYOUT_COMMON;
    ResourceLayout newLayout = ResourceLayout::LAYOUT_COMMON;
    ResourceSyncState prevSyncState = ResourceSyncState::NONE;
    ResourceSyncState newSyncState = ResourceSyncState::NONE;
};


// interpret any Bound as a numeric lower bound
static uint32_t boundLower(const Bound &b) {
    switch (b.type) {
    case BoundType::Exact: return b.value;
    case BoundType::From:  return b.value;
    case BoundType::UpTo:  return 0;
    case BoundType::All:   return 0;
    }
    return 0;
}

// interpret any Bound as a numeric upper bound
static uint32_t boundUpper(const Bound &b) {
    switch (b.type) {
    case BoundType::Exact: return b.value;
    case BoundType::UpTo:  return b.value;
    case BoundType::From:  // “>= value” means no finite upper -> inf
    case BoundType::All:   return std::numeric_limits<uint32_t>::max();
    }
    return std::numeric_limits<uint32_t>::max();
}

// pick the tighter (greater) lower bound
static Bound maxLower(const Bound &A, const Bound &B) {
    uint32_t a = boundLower(A);
    uint32_t b = boundLower(B);
    if (a > b) return A;
    if (b > a) return B;
    // tie -> prefer Exact > From > All/UpTo
    if (A.type == BoundType::Exact || B.type == BoundType::Exact)
        return A.type == BoundType::Exact ? A : B;
    if (A.type == BoundType::From || B.type == BoundType::From)
        return A.type == BoundType::From ? A : B;
    return A;  // both All or UpTo
}

// pick the tighter (smaller) upper bound
static Bound minUpper(const Bound &A, const Bound &B) {
    uint32_t a = boundUpper(A);
    uint32_t b = boundUpper(B);
    if (a < b) return A;
    if (b < a) return B;
    // tie -> prefer Exact > UpTo > All/From
    if (A.type == BoundType::Exact || B.type == BoundType::Exact)
        return A.type == BoundType::Exact ? A : B;
    if (A.type == BoundType::UpTo || B.type == BoundType::UpTo)
        return A.type == BoundType::UpTo ? A : B;
    return A;  // both All or From
}

// is this RangeSpec definitely empty?
static bool isEmpty(RangeSpec const &r) {
    // if the numeric lower ever exceeds the numeric upper, it's empty
    if (boundLower(r.mipLower)   > boundUpper(r.mipUpper))   return true;
    if (boundLower(r.sliceLower) > boundUpper(r.sliceUpper)) return true;
    return false;
}

// subtract the (assumed nonempty) 'cut' from 'orig', returning up to 4 remainders
static std::vector<RangeSpec> subtract(RangeSpec orig, RangeSpec cut) {
    std::vector<RangeSpec> out;
    // left strip: all mips below cut.mipLower
    if (boundLower(orig.mipLower) < boundLower(cut.mipLower)) {
        RangeSpec r = orig;
        r.mipUpper = { BoundType::UpTo, boundLower(cut.mipLower) - 1 };
        if (!isEmpty(r)) out.push_back(r);
    }
    // right strip: all mips above cut.mipUpper
    if (boundUpper(orig.mipUpper) > boundUpper(cut.mipUpper)) {
        RangeSpec r = orig;
        r.mipLower = { BoundType::From, boundUpper(cut.mipUpper) + 1 };
        if (!isEmpty(r)) out.push_back(r);
    }
    // now the middle in the mip dimension
    RangeSpec mid = orig;
    mid.mipLower = maxLower(orig.mipLower, cut.mipLower);
    mid.mipUpper = minUpper(orig.mipUpper, cut.mipUpper);

    // top strip: slices below cut.sliceLower
    if (boundLower(orig.sliceLower) < boundLower(cut.sliceLower)) {
        RangeSpec r = mid;
        r.sliceUpper = { BoundType::UpTo, boundLower(cut.sliceLower) - 1 };
        if (!isEmpty(r)) out.push_back(r);
    }
    // bottom strip: slices above cut.sliceUpper
    if (boundUpper(orig.sliceUpper) > boundUpper(cut.sliceUpper)) {
        RangeSpec r = mid;
        r.sliceLower = { BoundType::From, boundUpper(cut.sliceUpper) + 1 };
        if (!isEmpty(r)) out.push_back(r);
    }

    return out;
}

static RangeSpec intersect(RangeSpec A, RangeSpec B) {
    return {
        maxLower(A.mipLower,   B.mipLower),
        minUpper(A.mipUpper,   B.mipUpper),
        maxLower(A.sliceLower, B.sliceLower),
        minUpper(A.sliceUpper, B.sliceUpper)
    };
}

struct Segment {
    RangeSpec     rangeSpec;
    ResourceState state;
};

// 2) test if two 1D ranges [loA..upA] and [loB..upB] overlap or touch
static bool rangesOverlapOrTouch(const Bound &loA, const Bound &upA,
    const Bound &loB, const Bound &upB)
{
    uint64_t aLo = boundLower(loA), aUp = boundUpper(upA);
    uint64_t bLo = boundLower(loB), bUp = boundUpper(upB);
    return (aUp + 1 >= bLo) && (bUp + 1 >= aLo);
}

// 3) for union along one axis, build the new lower/upper Bound
static Bound unionLower(const Bound &A, const Bound &B) {
    uint32_t lo = (std::min)(boundLower(A), boundLower(B));
    return lo == 0
        ? Bound{BoundType::All, 0}
    : Bound{BoundType::From, lo};
}
static Bound unionUpper(const Bound &A, const Bound &B) {
    uint64_t up = std::max<uint64_t>(boundUpper(A), boundUpper(B));
    return up == (std::numeric_limits<uint32_t>::max)()
        ? Bound{BoundType::All, 0}
    : Bound{BoundType::UpTo, uint32_t(up)};
}

// 4) try to merge two segments; if successful, return the merged Segment
static std::optional<Segment> tryMerge(Segment const &A, Segment const &B) {
    if (!(A.state == B.state))
        return std::nullopt;

    // merge along the mip axis?
    if (A.rangeSpec.sliceLower == B.rangeSpec.sliceLower &&
        A.rangeSpec.sliceUpper == B.rangeSpec.sliceUpper &&
        rangesOverlapOrTouch(
            A.rangeSpec.mipLower,   A.rangeSpec.mipUpper,
            B.rangeSpec.mipLower,   B.rangeSpec.mipUpper))
    {
        RangeSpec R;
        R.sliceLower = A.rangeSpec.sliceLower;
        R.sliceUpper = A.rangeSpec.sliceUpper;
        R.mipLower   = unionLower(A.rangeSpec.mipLower, B.rangeSpec.mipLower);
        R.mipUpper   = unionUpper(A.rangeSpec.mipUpper, B.rangeSpec.mipUpper);
        return Segment{ R, A.state };
    }

    // merge along the slice axis?
    if (A.rangeSpec.mipLower == B.rangeSpec.mipLower &&
        A.rangeSpec.mipUpper == B.rangeSpec.mipUpper &&
        rangesOverlapOrTouch(
            A.rangeSpec.sliceLower, A.rangeSpec.sliceUpper,
            B.rangeSpec.sliceLower, B.rangeSpec.sliceUpper))
    {
        RangeSpec R;
        R.mipLower   = A.rangeSpec.mipLower;
        R.mipUpper   = A.rangeSpec.mipUpper;
        R.sliceLower = unionLower(A.rangeSpec.sliceLower, B.rangeSpec.sliceLower);
        R.sliceUpper = unionUpper(A.rangeSpec.sliceUpper, B.rangeSpec.sliceUpper);
        return Segment{ R, A.state };
    }

    return std::nullopt;
}

// the top-level mergeSymbolic:
static void mergeSymbolic(std::vector<Segment> &segs) {
    // sort by (sliceLower, sliceUpper, mipLower, mipUpper) numeric order
    std::sort(segs.begin(), segs.end(),
        [](auto const &L, auto const &R){
            auto tL = std::tie(
                boundLower(L.rangeSpec.sliceLower),
                boundUpper(L.rangeSpec.sliceUpper),
                boundLower(L.rangeSpec.mipLower),
                boundUpper(L.rangeSpec.mipUpper));
            auto tR = std::tie(
                boundLower(R.rangeSpec.sliceLower),
                boundUpper(R.rangeSpec.sliceUpper),
                boundLower(R.rangeSpec.mipLower),
                boundUpper(R.rangeSpec.mipUpper));
            return tL < tR;
        });

    // sweep & merge
    std::vector<Segment> out;
    out.reserve(segs.size());
    for (auto &seg : segs) {
        if (!out.empty()) {
            if (auto m = tryMerge(out.back(), seg)) {
                out.back() = *m;
                continue;
            }
        }
        out.push_back(seg);
    }
    segs.swap(out);
}

class SymbolicTracker {
    std::vector<Segment> _segs;
public:
    SymbolicTracker(RangeSpec whole, ResourceState init) {
        _segs.push_back({ whole, init });
    }

    // apply a new requirement and emit transitions
    void apply(RangeSpec want,
        ResourceState newState,
        std::shared_ptr<Resource> pRes,
        std::vector<ResourceTransition>& out)
    {
        std::vector<Segment> next;
        for (auto &seg : _segs) {
            auto cut = intersect(seg.rangeSpec, want);
            if (isEmpty(cut)) {
                // no overlap: keep seg as-is
                next.push_back(seg);
            } else {
                // split seg by cut
                for (auto &rem : subtract(seg.rangeSpec, cut))
                    next.push_back({ rem, seg.state });

                // record a transition over 'cut' if state differs
                if (!(seg.state == newState)) {
                    out.push_back({
                        pRes, // resource
                        cut,
                        seg.state.access,
                        newState.access,
                        seg.state.layout,
                        newState.layout,
                        seg.state.sync,
                        newState.sync
                        });
                }
            }
        }

        // insert the new-state segment
        next.push_back({ want, newState });

        // merge back any adjacent segments with identical state & identical RangeSpec
        mergeSymbolic(next);
        _segs.swap(next);
    }
};