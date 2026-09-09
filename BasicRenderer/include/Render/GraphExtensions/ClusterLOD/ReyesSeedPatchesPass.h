#pragma once

#include <memory>

#include <rhi.h>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class ResourceGroup; }
using org::ResourceGroup;

class ReyesSeedPatchesPass final : public org::TypedRenderGraphPass<ReyesSeedPatchesPass, br::render::PreparedComputeIndirect> {
public:
    ReyesSeedPatchesPass(
        std::shared_ptr<Buffer> visibleClustersBuffer,
        std::shared_ptr<Buffer> ownedClustersBuffer,
        std::shared_ptr<Buffer> ownedClustersCounterBuffer,
        std::shared_ptr<Buffer> splitQueueBuffer,
        std::shared_ptr<Buffer> splitQueueCounterBuffer,
        std::shared_ptr<Buffer> splitQueueOverflowBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        std::shared_ptr<ResourceGroup> slabResourceGroup,
        uint32_t maxSplitQueueEntries,
        uint32_t phaseIndex);

    void Declare(org::PassBuilder& builder);
    br::render::PreparedComputeIndirect Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeIndirect&, org::PassRecordContext&);
    void Update(const UpdateExecutionContext& executionContext) override;

private:
    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_ownedClustersBuffer;
    std::shared_ptr<Buffer> m_ownedClustersCounterBuffer;
    std::shared_ptr<Buffer> m_splitQueueBuffer;
    std::shared_ptr<Buffer> m_splitQueueCounterBuffer;
    std::shared_ptr<Buffer> m_splitQueueOverflowBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    ResourceBindingToken m_indirectArgumentsBinding{};
    std::shared_ptr<ResourceGroup> m_slabResourceGroup;
    uint32_t m_maxSplitQueueEntries = 0u;
    uint32_t m_phaseIndex = 0u;
    PipelineState m_pso;
    std::shared_ptr<rhi::CommandSignaturePtr> m_commandSignature;
};
