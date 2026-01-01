// ImmediateOps.h
#pragma once
#include <cstdint>
#include "ImmediateBytecode.h"
#include "Resources/ResourceStateTracker.h"

namespace rg::imm
{
    struct CopyBufferPayload {
        uint64_t srcId, dstId;
        uint64_t srcOffset, dstOffset;
        uint64_t numBytes;
    };

    struct CopyTextureSubresourcePayload {
        uint64_t srcId, dstId;
        uint16_t srcSubresource, dstSubresource;
        uint32_t _pad = 0;
    };

    struct Box3D {
        uint32_t left, top, front;
        uint32_t right, bottom, back;
    };

    struct CopyTextureRegionsPayload {
        uint64_t srcId, dstId;
        uint16_t srcSubresource, dstSubresource;
        uint32_t regionCount;     // followed by regionCount * Box3D
        // Box3D regions[];
    };

    struct ClearRTVPayload {
        uint64_t targetId;
        RangeSpec range;      // for tracking; actual clear may still be whole-view
        float rgba[4];
    };

    struct ClearDSVPayload {
        uint64_t targetId;
        RangeSpec range;
        float depth;
        uint8_t stencil;
        uint8_t clearDepth;
        uint8_t clearStencil;
        uint8_t _pad;
    };

    struct ClearUAV_U32x4Payload {
        uint64_t targetId;
        RangeSpec range;
        uint32_t v[4];
    };

    struct ClearUAV_F32x4Payload {
        uint64_t targetId;
        RangeSpec range;
        float v[4];
    };

    struct ResolveSubresourcePayload {
        uint64_t srcId, dstId;
        uint16_t srcSubresource, dstSubresource;
        uint32_t format; // rhi::Format or your own
    };

    struct UAVBarrierPayload {
        uint64_t targetId;
    };
}
