#include "Render/PassBuilders.h"

std::vector<ResourceAndRange>
expandToRanges(ResourceIdentifierAndRange const & rir, RenderGraph* graph)
{
    if (!rir.identifier.IsCustom() && !rir.identifier.IsBuiltin()) {
        // invalid identifier -> return empty
        return {};
    }

    auto resPtr = graph->RequestResource(rir.identifier);
    if (!resPtr) return {};

    // Now wrap that actual resource + rir.range into a ResourceAndRange:
    ResourceAndRange actualRAR(resPtr);
    actualRAR.range    = rir.range;
    return { actualRAR };
}