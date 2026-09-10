#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

struct VirtualShadowMapBuildMarkTilesBindings {
    org::ResourceBindingToken tileWork;
    org::ResourceBindingToken tileCount;
};

class VirtualShadowMapBuildMarkTilesPass final : public org::TypedRenderGraphPass<VirtualShadowMapBuildMarkTilesPass,
    br::render::PreparedComputeDispatch, VirtualShadowMapBuildMarkTilesBindings> {
public:
    VirtualShadowMapBuildMarkTilesPass(
        std::shared_ptr<Buffer> tileWorkBuffer,
        std::shared_ptr<Buffer> tileCountBuffer);

    VirtualShadowMapBuildMarkTilesBindings Declare(org::PassBuilder& builder);
    void Initialize();
    void Update(const UpdateExecutionContext& executionContext) override;
    br::render::PreparedComputeDispatch Prepare(const VirtualShadowMapBuildMarkTilesBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const VirtualShadowMapBuildMarkTilesBindings&,
        const br::render::PreparedComputeDispatch&, org::PassRecordContext&);
    void ShutdownPass();

private:
    PipelineState m_pso;
    std::shared_ptr<Buffer> m_tileWorkBuffer;
    std::shared_ptr<Buffer> m_tileCountBuffer;
};
