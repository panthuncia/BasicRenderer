#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Resources/Buffers/Buffer.h"

namespace org { class Buffer; }
using org::Buffer;

struct PreparedReyesCounterCopy {
    org::PreparedResourceReference source;
    org::PreparedResourceReference destination;
};

class ReyesCopyCounterPass final
    : public org::TypedRenderGraphPass<ReyesCopyCounterPass, PreparedReyesCounterCopy> {
public:
    ReyesCopyCounterPass(std::shared_ptr<Buffer> sourceCounterBuffer, std::shared_ptr<Buffer> destCounterBuffer)
        : m_sourceCounterBuffer(std::move(sourceCounterBuffer))
        , m_destCounterBuffer(std::move(destCounterBuffer))
    {
    }

    void Declare(org::PassBuilder& builder)
    {
        m_sourceBinding = builder.BindCopySource(m_sourceCounterBuffer);
        m_destinationBinding = builder.BindCopyDestination(m_destCounterBuffer);
        builder.PreferQueue(QueueKind::Copy);
    }

    void Initialize() {}

    PreparedReyesCounterCopy Prepare(const org::PassPrepareContext& preparation)
    {
        return {
            preparation.CaptureResource(m_sourceBinding),
            preparation.CaptureResource(m_destinationBinding),
        };
    }

    static void Record(const PreparedReyesCounterCopy& data,
        org::PassRecordContext& recording)
    {
        recording.Commands().CopyBufferRegion(
            recording.Resolve(data.destination).GetHandle(), 0,
            recording.Resolve(data.source).GetHandle(), 0, sizeof(uint32_t));
    }

private:
    std::shared_ptr<Buffer> m_sourceCounterBuffer;
    std::shared_ptr<Buffer> m_destCounterBuffer;
    org::ResourceBindingToken m_sourceBinding;
    org::ResourceBindingToken m_destinationBinding;
};
