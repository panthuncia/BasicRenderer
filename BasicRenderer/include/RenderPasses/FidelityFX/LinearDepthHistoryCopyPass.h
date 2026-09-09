#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Render/RenderContext.h"
#include "Managers/ViewManager.h"
#include <vector>

class LinearDepthHistoryCopyPass
    : public org::TypedRenderGraphPass<LinearDepthHistoryCopyPass> {
public:
    explicit LinearDepthHistoryCopyPass(ViewManager* viewManager)
        : m_viewManager(viewManager) {
    }

    void Declare(org::PassBuilder& builder) {
        // The current depth pyramid remains intact until the next frame's
        // phase-1 cull consumes it. Declaring the read keeps this marker after
        // the final phase-2 depth writes without copying the texture.
        builder.WithShaderResource(Builtin::LinearDepthMaps);
    }

    org::EmptyPassFrameData Prepare(const org::PassPrepareContext& preparation) {
        if (m_viewManager) preparation.Reserve(m_viewManager->ReserveDepthHistoryPublication());
        return {};
    }
    static void Record(const org::EmptyPassFrameData&, org::PassRecordContext&) {}

private:
    ViewManager* m_viewManager = nullptr;
};
