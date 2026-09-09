#pragma once

#include <memory>
#include <vector>

#include "Interfaces/IDynamicDeclaredResources.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedResourceClears.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class ClearDeepVisibilityPass final : public org::TypedRenderGraphPass<ClearDeepVisibilityPass, br::render::PreparedResourceClears>, public IDynamicDeclaredResources {
public:
    ClearDeepVisibilityPass(
        std::shared_ptr<Buffer> deepVisibilityCounterBuffer,
        std::shared_ptr<Buffer> deepVisibilityOverflowCounterBuffer,
        std::shared_ptr<Buffer> deepVisibilityStatsBuffer);

    void Declare(org::PassBuilder& builder);
    void Update(const UpdateExecutionContext& executionContext) override;
    bool DeclaredResourcesChanged() const override;
    br::render::PreparedResourceClears Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedResourceClears&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_deepVisibilityCounterBuffer;
    std::shared_ptr<Buffer> m_deepVisibilityOverflowCounterBuffer;
    std::shared_ptr<Buffer> m_deepVisibilityStatsBuffer;
    std::vector<std::shared_ptr<PixelBuffer>> m_headPointerTextures;
    bool m_declaredResourcesChanged = true;
};
