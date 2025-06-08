#include "Render/PassBuilders.h"

std::vector<ResourceAndRange>
expandToRanges(ResourceIdentifierAndRange const & rir, RenderGraph* graph)
{
    auto resPtr = graph->RequestResource(rir.identifier);
    if (!resPtr) return {};

    // Now wrap that actual resource + rir.range into a ResourceAndRange:
    ResourceAndRange actualRAR(resPtr);
    actualRAR.range    = rir.range;
    return { actualRAR };
}