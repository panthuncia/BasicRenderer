// ImmediateResourceResolve.h
#pragma once
#include <cstdint>
#include "Resources/ResourceIdentifier.h"
#include "Resources/ResourceStateTracker.h"

class Resource;
class RenderGraph;

namespace rg::imm
{
    struct ResolvedRes {
        uint64_t id = 0;     // Resource::GetGlobalResourceID()
        RangeSpec range{};   // for tracking/subresource transitions
    };

    // Overloads
    ResolvedRes Resolve(RenderGraph& rg, Resource* r, const RangeSpec& range);
    ResolvedRes Resolve(RenderGraph& rg, const ResourceIdentifier& rid, const RangeSpec& range);
    ResolvedRes Resolve(RenderGraph& rg, const ResourceIdentifierAndRange& rr);
}
