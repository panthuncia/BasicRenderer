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

class ReyesBuildRasterWorkPass final : public org::TypedRenderGraphPass<ReyesBuildRasterWorkPass, br::render::PreparedComputeIndirect> {
public:
    ReyesBuildRasterWorkPass(
        std::shared_ptr<Buffer> diceQueueBuffer,
        std::shared_ptr<Buffer> diceQueueCounterBuffer,
        std::shared_ptr<Buffer> diceQueueReadOffsetBuffer,
        std::shared_ptr<Buffer> tessTableConfigsBuffer,
        std::shared_ptr<Buffer> rasterWorkBuffer,
        std::shared_ptr<Buffer> rasterWorkCounterBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        std::shared_ptr<Buffer> telemetryBuffer,
        uint32_t rasterWorkCapacity,
        uint32_t phaseIndex = 0u,
        std::shared_ptr<Buffer> visibleClustersBuffer = nullptr,
        std::shared_ptr<Buffer> visibleClusterTransformIndicesBuffer = nullptr,
        std::shared_ptr<Buffer> viewDepthSrvIndicesBuffer = nullptr,
        std::shared_ptr<Buffer> replayDiceQueueBuffer = nullptr,
        std::shared_ptr<Buffer> replayDiceQueueCounterBuffer = nullptr,
        std::shared_ptr<Buffer> replayDiceQueueOverflowBuffer = nullptr,
        uint32_t replayDiceQueueCapacity = 0u,
        std::shared_ptr<ResourceGroup> slabResourceGroup = nullptr);

    void Declare(org::PassBuilder& builder);
    void Update(const UpdateExecutionContext& executionContext) override;
    br::render::PreparedComputeIndirect Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeIndirect&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_diceQueueBuffer;
    std::shared_ptr<Buffer> m_diceQueueCounterBuffer;
    std::shared_ptr<Buffer> m_diceQueueReadOffsetBuffer;
    std::shared_ptr<Buffer> m_tessTableConfigsBuffer;
    std::shared_ptr<Buffer> m_rasterWorkBuffer;
    std::shared_ptr<Buffer> m_rasterWorkCounterBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    ResourceBindingToken m_indirectArgumentsBinding{};
    std::shared_ptr<Buffer> m_telemetryBuffer;
    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_visibleClusterTransformIndicesBuffer;
    std::shared_ptr<Buffer> m_viewDepthSrvIndicesBuffer;
    std::shared_ptr<Buffer> m_replayDiceQueueBuffer;
    std::shared_ptr<Buffer> m_replayDiceQueueCounterBuffer;
    std::shared_ptr<Buffer> m_replayDiceQueueOverflowBuffer;
    std::shared_ptr<ResourceGroup> m_slabResourceGroup;
    uint32_t m_rasterWorkCapacity = 0u;
    uint32_t m_phaseIndex = 0u;
    uint32_t m_replayDiceQueueCapacity = 0u;
    PipelineState m_pso;
    std::shared_ptr<rhi::CommandSignaturePtr> m_commandSignature;
};
