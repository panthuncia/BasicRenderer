#pragma once
#include "Render/ImmediateExecution/ImmediateBytecode.h"
#include "Render/ImmediateExecution/ImmediateOps.h"
#include "Render/ImmediateExecution/ImmediateAccess.h"
#include "Render/ImmediateExecution/ImmediateResourceResolve.h"

class RenderGraph;

namespace rg::imm
{
    // Helper: map op intent -> desired ResourceState
    inline ResourceState State_CopySrc() {
        ResourceState s{};
        s.access = rhi::ResourceAccessType::CopySource;
        s.layout = rhi::ResourceLayout::CopySource;
        return s;
    }
    inline ResourceState State_CopyDst() {
        ResourceState s{};
        s.access = rhi::ResourceAccessType::CopyDest;
        s.layout = rhi::ResourceLayout::CopyDest;
        return s;
    }
    inline ResourceState State_RTV() {
        ResourceState s{};
        s.access = rhi::ResourceAccessType::RenderTarget;
        s.layout = rhi::ResourceLayout::RenderTarget;
        return s;
    }
    inline ResourceState State_DSV() {
        ResourceState s{};
        s.access = rhi::ResourceAccessType::DepthReadWrite;
        s.layout = rhi::ResourceLayout::DepthReadWrite;
        return s;
    }
    inline ResourceState State_UAV() {
        ResourceState s{};
        s.access = rhi::ResourceAccessType::UnorderedAccess;
        s.layout = rhi::ResourceLayout::UnorderedAccess;
        return s;
    }

    struct RecordTargets {
        BytecodeWriter& code;
        AccessLog& access;
    };

    class ImmediateCommandList {
    public:
        ImmediateCommandList(RenderGraph& rg, RecordTargets out, Queue q)
            : rg_(rg), out_(out), q_(q) {
        }

        // ----- Copy buffer -----
        void CopyBuffer(Resource* dst, uint64_t dstOffset,
            Resource* src, uint64_t srcOffset,
            uint64_t numBytes,
            RangeSpec srcRange = {}, RangeSpec dstRange = {})
        {
            const auto s = Resolve(rg_, src, srcRange);
            const auto d = Resolve(rg_, dst, dstRange);

            out_.access.Push({ s.id, s.range, State_CopySrc(), q_ });
            out_.access.Push({ d.id, d.range, State_CopyDst(), q_ });

            CopyBufferPayload p{ s.id, d.id, srcOffset, dstOffset, numBytes };
            out_.code.Emit(Op::CopyBuffer, q_, p);
        }

        void CopyBuffer(const ResourceIdentifier& dst, uint64_t dstOffset,
            const ResourceIdentifier& src, uint64_t srcOffset,
            uint64_t numBytes,
            RangeSpec srcRange = {}, RangeSpec dstRange = {})
        {
            const auto s = Resolve(rg_, src, srcRange);
            const auto d = Resolve(rg_, dst, dstRange);

            out_.access.Push({ s.id, s.range, State_CopySrc(), q_ });
            out_.access.Push({ d.id, d.range, State_CopyDst(), q_ });

            CopyBufferPayload p{ s.id, d.id, srcOffset, dstOffset, numBytes };
            out_.code.Emit(Op::CopyBuffer, q_, p);
        }

        // ----- Copy texture subresource (fixed payload) -----
        void CopyTextureSubresource(const ResourceIdentifier& dst, uint16_t dstSubres,
            const ResourceIdentifier& src, uint16_t srcSubres,
            RangeSpec srcRange = {}, RangeSpec dstRange = {})
        {
            const auto s = Resolve(rg_, src, srcRange);
            const auto d = Resolve(rg_, dst, dstRange);

            out_.access.Push({ s.id, s.range, State_CopySrc(), q_ });
            out_.access.Push({ d.id, d.range, State_CopyDst(), q_ });

            CopyTextureSubresourcePayload p{};
            p.srcId = s.id; p.dstId = d.id;
            p.srcSubresource = srcSubres; p.dstSubresource = dstSubres;
            out_.code.Emit(Op::CopyTextureSubresource, q_, p);
        }

        // ----- Copy texture regions (variable payload; future-proof) -----
        void CopyTextureRegions(const ResourceIdentifier& dst, uint16_t dstSubres,
            const ResourceIdentifier& src, uint16_t srcSubres,
            std::span<const Box3D> regions,
            RangeSpec srcRange = {}, RangeSpec dstRange = {})
        {
            const auto s = Resolve(rg_, src, srcRange);
            const auto d = Resolve(rg_, dst, dstRange);

            out_.access.Push({ s.id, s.range, State_CopySrc(), q_ });
            out_.access.Push({ d.id, d.range, State_CopyDst(), q_ });

            const size_t start = out_.code.Begin(Op::CopyTextureRegions, q_);
            CopyTextureRegionsPayload head{};
            head.srcId = s.id; head.dstId = d.id;
            head.srcSubresource = srcSubres;
            head.dstSubresource = dstSubres;
            head.regionCount = static_cast<uint32_t>(regions.size());
            out_.code.Write(head);
            out_.code.WriteSpan(regions);
            out_.code.End(start);
        }

        // ----- Clears -----
        void ClearRTV(Resource* target, const float rgba[4], RangeSpec range = {}) {
            const auto t = Resolve(rg_, target, range);
            out_.access.Push({ t.id, t.range, State_RTV(), q_ });

            ClearRTVPayload p{};
            p.targetId = t.id;
            p.range = t.range;
            p.rgba[0] = rgba[0]; p.rgba[1] = rgba[1]; p.rgba[2] = rgba[2]; p.rgba[3] = rgba[3];
            out_.code.Emit(Op::ClearRTV, q_, p);
        }

        void ClearDSV(const ResourceIdentifierAndRange& target, float depth, uint8_t stencil,
            bool clearDepth, bool clearStencil)
        {
            const auto t = Resolve(rg_, target);
            out_.access.Push({ t.id, t.range, State_DSV(), q_ });

            ClearDSVPayload p{};
            p.targetId = t.id;
            p.range = t.range;
            p.depth = depth;
            p.stencil = stencil;
            p.clearDepth = clearDepth ? 1 : 0;
            p.clearStencil = clearStencil ? 1 : 0;
            out_.code.Emit(Op::ClearDSV, q_, p);
        }

        void ClearUAV(Resource* target, const uint32_t v[4], RangeSpec range = {}) {
            const auto t = Resolve(rg_, target, range);
            out_.access.Push({ t.id, t.range, State_UAV(), q_ });

            ClearUAV_U32x4Payload p{};
            p.targetId = t.id; p.range = t.range;
            p.v[0] = v[0]; p.v[1] = v[1]; p.v[2] = v[2]; p.v[3] = v[3];
            out_.code.Emit(Op::ClearUAV_U32x4, q_, p);
        }

        void ClearUAV(const ResourceIdentifier& target, const float v[4], RangeSpec range = {}) {
            const auto t = Resolve(rg_, target, range);
            out_.access.Push({ t.id, t.range, State_UAV(), q_ });

            ClearUAV_F32x4Payload p{};
            p.targetId = t.id; p.range = t.range;
            p.v[0] = v[0]; p.v[1] = v[1]; p.v[2] = v[2]; p.v[3] = v[3];
            out_.code.Emit(Op::ClearUAV_F32x4, q_, p);
        }

        // ----- Resolve / UAV barrier -----
        void ResolveSubresource(const ResourceIdentifier& dst, uint16_t dstSub,
            const ResourceIdentifier& src, uint16_t srcSub,
            uint32_t format,
            RangeSpec srcRange = {}, RangeSpec dstRange = {})
        {
            const auto s = Resolve(rg_, src, srcRange);
            const auto d = Resolve(rg_, dst, dstRange);

            out_.access.Push({ s.id, s.range, State_CopySrc(), q_ });
            out_.access.Push({ d.id, d.range, State_CopyDst(), q_ });

            ResolveSubresourcePayload p{};
            p.srcId = s.id; p.dstId = d.id;
            p.srcSubresource = srcSub; p.dstSubresource = dstSub;
            p.format = format;
            out_.code.Emit(Op::ResolveSubresource, q_, p);
        }

        void UAVBarrier(Resource* target) {
            const auto t = Resolve(rg_, target, RangeSpec{});
            // Conservatively keep it in UAV state
            out_.access.Push({ t.id, t.range, State_UAV(), q_ });

            UAVBarrierPayload p{ t.id };
            out_.code.Emit(Op::UAVBarrier, q_, p);
        }

    private:
        RenderGraph& rg_;
        RecordTargets out_;
        Queue         q_;
    };
}
