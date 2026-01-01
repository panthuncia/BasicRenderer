#include "Render/ImmediateExecution/ImmediateResourceResolve.h"
#include "Render/RenderGraph.h"
#include "Resources/ResourceStateTracker.h"

using namespace rg::imm;

ResolvedRes rg::imm::Resolve(RenderGraph& rg, Resource* r, const RangeSpec& range) {
    assert(r);
    ResolvedRes out{};
    out.id = r->GetGlobalResourceID();
    out.range = range;
    return out;
}

ResolvedRes rg::imm::Resolve(RenderGraph& rg, const ResourceIdentifier& rid, const RangeSpec& range) {
    auto sp = rg.RequestResource(rid, /*allowFailure=*/false); // bypasses view
    if (!sp) throw std::runtime_error("Immediate Resolve failed: " + rid.ToString());
    ResolvedRes out{};
    out.id = sp->GetGlobalResourceID();
    out.range = range;
    return out;
}

ResolvedRes rg::imm::Resolve(RenderGraph& rg, const ResourceIdentifierAndRange& rr) {
    return Resolve(rg, rr.identifier, rr.range);
}
