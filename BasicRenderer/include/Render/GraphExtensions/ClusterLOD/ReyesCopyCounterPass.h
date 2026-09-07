#pragma once

#include <memory>

#include "RenderPasses/Base/CopyPass.h"
#include "Resources/Buffers/Buffer.h"

namespace org { class Buffer; }
using org::Buffer;

class ReyesCopyCounterPass final : public CopyPass, public IHasImmediateModeCommands {
public:
    ReyesCopyCounterPass(std::shared_ptr<Buffer> sourceCounterBuffer, std::shared_ptr<Buffer> destCounterBuffer)
        : m_sourceCounterBuffer(std::move(sourceCounterBuffer))
        , m_destCounterBuffer(std::move(destCounterBuffer))
    {
    }

    void DeclareResourceUsages(CopyPassBuilder* builder) override
    {
        builder->WithCopySource(m_sourceCounterBuffer)
            .WithCopyDest(m_destCounterBuffer)
            .PreferQueue(QueueKind::Copy);
    }

    void Setup() override {}

    void RecordImmediateCommands(ImmediateExecutionContext& context) override
    {
        context.list.CopyBufferRegion(m_destCounterBuffer.get(), 0, m_sourceCounterBuffer.get(), 0, sizeof(uint32_t));
    }

    PassReturn Execute(PassExecutionContext& context) override
    {
        (void)context;
        return {};
    }

    PreparedPass PrepareFrame(FramePreparationContext&) override
    {
        struct CopyData {
            rhi::Resource source, destination;
            std::shared_ptr<const void> sourceOwner, destinationOwner;
        };
        auto source = m_sourceCounterBuffer->CaptureBackingAllocation();
        auto destination = m_destCounterBuffer->CaptureBackingAllocation();
        if (!source || !destination) return {};
        auto record = +[](const CopyData& data, RecordingContext& recording) {
            recording.Commands().CopyBufferRegion(
                data.destination.GetHandle(), 0, data.source.GetHandle(), 0, sizeof(uint32_t));
        };
        CopyData data{
            .source = source.resource,
            .destination = destination.resource,
            .sourceOwner = std::move(source.lease),
            .destinationOwner = std::move(destination.lease),
        };
        return PreparedPass::Make(std::move(data), record);
    }

    void Cleanup() override {}

private:
    std::shared_ptr<Buffer> m_sourceCounterBuffer;
    std::shared_ptr<Buffer> m_destCounterBuffer;
};
