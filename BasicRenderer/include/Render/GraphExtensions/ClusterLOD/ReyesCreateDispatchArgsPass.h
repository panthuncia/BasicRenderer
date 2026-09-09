#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

class ReyesCreateDispatchArgsPass final : public org::TypedRenderGraphPass<ReyesCreateDispatchArgsPass, br::render::PreparedComputeDispatch> {
public:
    ReyesCreateDispatchArgsPass(
        std::shared_ptr<Buffer> sourceCounterBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        std::shared_ptr<Buffer> sourceBaseCounterBuffer = nullptr,
        uint32_t threadsPerGroup = 64u,
        uint32_t maxWorkItemCount = 0xFFFFFFFFu);

    void Declare(org::PassBuilder& builder);
    void Initialize();
    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeDispatch&, org::PassRecordContext&);
    void Update(const UpdateExecutionContext& executionContext) override;
    void ShutdownPass();

private:
    PipelineState m_pso;
    std::shared_ptr<Buffer> m_sourceCounterBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    std::shared_ptr<Buffer> m_sourceBaseCounterBuffer;
    uint32_t m_threadsPerGroup = 64u;
    uint32_t m_maxWorkItemCount = 0xFFFFFFFFu;
};
