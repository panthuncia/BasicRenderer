#include "Render/PassBuilders.h"

#include "Render/RenderGraphBuilder.h"

std::vector<ResourceAndRange>
expandToRanges(ResourceIdentifierAndRange const & rir, RenderGraphBuilder* builder)
{
    if (!rir.identifier.IsCustom() && !rir.identifier.IsBuiltin()) {
        // invalid identifier -> return empty
        return {};
    }

    auto resPtr = builder->RequestResource(rir.identifier);
    if (!resPtr) return {};

    // Now wrap that actual resource + rir.range into a ResourceAndRange:
    ResourceAndRange actualRAR(resPtr);
    actualRAR.range    = rir.range;
    return { actualRAR };
}