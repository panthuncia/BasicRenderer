#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <mutex>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Render/GraphExtensions/ClusterLOD/VirtualShadowUpgradeService.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct VirtualShadowMapAdmitPagesBindings {
    org::ResourceBindingToken pageTable, dirtyPageFlags, pageMetadata, clipmapInfo, compactShadowCameras, stats;
    std::vector<org::ResourceBindingToken> upgradeInputs;
    uint32_t normalBudget = 0;
    uint32_t upgradeBudget = 0;
};

class VirtualShadowMapAdmitPagesPass final : public org::TypedRenderGraphPass<VirtualShadowMapAdmitPagesPass,
    br::render::PreparedComputePipelineSequence, VirtualShadowMapAdmitPagesBindings> {
public:
    VirtualShadowMapAdmitPagesPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
        std::vector<std::shared_ptr<Buffer>> upgradeInputBuffers,
        std::shared_ptr<Buffer> pageMetadataBuffer,
        std::shared_ptr<Buffer> clipmapInfoBuffer,
        std::shared_ptr<Buffer> compactShadowCamerasBuffer,
        std::shared_ptr<Buffer> statsBuffer,
        VirtualShadowUpgradeQueue upgradeQueue);

    VirtualShadowMapAdmitPagesBindings Declare(org::PassBuilder& builder);
    br::render::PreparedComputePipelineSequence Prepare(const VirtualShadowMapAdmitPagesBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const VirtualShadowMapAdmitPagesBindings&,
        const br::render::PreparedComputePipelineSequence&, org::PassRecordContext&);

private:
    PipelineState m_pso;
    PipelineState m_applyUpgradesPso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_dirtyPageFlagsBuffer;
    std::vector<std::shared_ptr<Buffer>> m_upgradeInputBuffers;
    std::shared_ptr<Buffer> m_pageMetadataBuffer;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
    std::shared_ptr<Buffer> m_compactShadowCamerasBuffer;
    std::shared_ptr<Buffer> m_statsBuffer;
    VirtualShadowUpgradeQueue m_upgradeQueue;
};
