#include "Render/ImmediateExecution/ImmediateCommandList.h"

namespace rg::imm {

	// BytecodeReader functions
	bool BytecodeReader::Empty() const noexcept { return cur >= end; }

    Op BytecodeReader::ReadOp() {
        Require(1);
        Op op = static_cast<Op>(*cur);
        cur += 1;
        return op;
    }

    void BytecodeReader::Require(size_t n) const {
        if (cur + n > end) {
            throw std::runtime_error("Immediate bytecode underflow");
        }
    }

    void BytecodeReader::Align(size_t a) {
        if (a == 0) return;
        uintptr_t ip = reinterpret_cast<uintptr_t>(cur);
        uintptr_t aligned = (ip + (a - 1)) & ~(uintptr_t(a - 1));
        cur = reinterpret_cast<std::byte const*>(aligned);
    }

	// End of BytecodeReader functions

    void Replay(std::vector<std::byte> const& bytecode, rhi::CommandList& cl) {
        BytecodeReader r(bytecode.data(), bytecode.size());
        while (!r.Empty()) {
            Op op = r.ReadOp();
            switch (op) {
            case Op::CopyBufferRegion: {
                auto cmd = r.ReadPOD<CopyBufferRegionCmd>();
                cl.CopyBufferRegion(cmd.dst, cmd.dstOffset, cmd.src, cmd.srcOffset, cmd.numBytes);
                break;
            }
            case Op::ClearRTV: {
                auto cmd = r.ReadPOD<ClearRTVCmd>();
                cl.ClearRenderTargetView(cmd.rtv, cmd.clear);
                break;
            }
            case Op::ClearDSV: {
                auto cmd = r.ReadPOD<ClearDSVCmd>();
                cl.ClearDepthStencilView(cmd.dsv, cmd.clearDepth, cmd.depth, cmd.clearStencil, cmd.stencil);
                break;
            }
            case Op::ClearUavFloat: {
                auto cmd = r.ReadPOD<ClearUavFloatCmd>();
                cl.ClearUavFloat(cmd.info, cmd.value);
                break;
            }
            case Op::ClearUavUint: {
                auto cmd = r.ReadPOD<ClearUavUintCmd>();
                cl.ClearUavUint(cmd.info, cmd.value);
                break;
            }
            case Op::CopyTextureRegion: {
                auto cmd = r.ReadPOD<CopyTextureRegionCmd>();
                cl.CopyTextureRegion(cmd.dst, cmd.src);
                break;
            }
            case Op::CopyTextureToBuffer: {
                auto cmd = r.ReadPOD<CopyTextureToBufferCmd>();
                cl.CopyTextureToBuffer(cmd.region);
                break;
            }
            case Op::CopyBufferToTexture: {
                auto cmd = r.ReadPOD<CopyBufferToTextureCmd>();
                cl.CopyBufferToTexture(cmd.region);
                break;
            }
            default:
                throw std::runtime_error("Unknown immediate bytecode op");
            }
        }
    }

	// ImmediateCommandList functions

    void ImmediateCommandList::Reset() {
        m_writer.Reset();
        m_ptrs.clear();
        m_trackers.clear();
    }

    FrameData ImmediateCommandList::Finalize() const {
        FrameData out;
        out.bytecode = m_writer.data;

        // Convert trackers -> requirements (skip "Common/Common" regions).
        out.requirements.reserve(64);
        for (auto const& [rid, tracker] : m_trackers) {
            auto it = m_ptrs.find(rid);
            if (it == m_ptrs.end() || !it->second) continue;

            // Initial "no-op" state:
            ResourceState init{
                rhi::ResourceAccessType::Common,
                rhi::ResourceLayout::Common,
                rhi::ResourceSyncState::None
            };

            for (auto const& seg : tracker.GetSegments()) {
                if (seg.state == init) continue; // ignore untouched portions
                ResourceRequirement rr{ it->second };
                rr.resourceAndRange.range = seg.rangeSpec;
                rr.state = seg.state;
                out.requirements.push_back(std::move(rr));
            }
        }
        return out;
    }

    ImmediateCommandList::Resolved ImmediateCommandList::Resolve(ResourceIdentifier const& id) {
        if (!m_resolveFn) {
            throw std::runtime_error("ImmediateCommandList has no ResolveByIdFn");
        }
        auto sp = m_resolveFn(m_resolveUser, id, /*allowFailure=*/false);
        if (!sp) {
            throw std::runtime_error("ImmediateCommandList failed to resolve id: " + id.ToString());
        }
        return Resolve(sp.get(), sp);
    }

    ImmediateCommandList::Resolved ImmediateCommandList::Resolve(Resource* p) {
        if (!p) { 
            throw std::runtime_error("ImmediateCommandList: null Resource*"); 
        }
        // TODO: fix this shared_ptr nonsense
        std::shared_ptr<Resource> sp = p->shared_from_this();
        return Resolve(p, std::move(sp));
    }

    ImmediateCommandList::Resolved ImmediateCommandList::Resolve(Resource* p, std::shared_ptr<Resource> keepAlive) {
        Resolved out;
        out.keepAlive = std::move(keepAlive);
        out.raw = p;
        out.globalId = p->GetGlobalResourceID();

        // Cache keepalive for requirement building
        m_ptrs[out.globalId] = out.keepAlive;

        if (m_trackers.find(out.globalId) == m_trackers.end()) {
            RangeSpec whole{};
            ResourceState init{
                rhi::ResourceAccessType::Common,
                rhi::ResourceLayout::Common,
                rhi::ResourceSyncState::None
            };
            m_trackers.emplace(out.globalId, SymbolicTracker(whole, init));
        }
        return out;
    }

    ResourceState ImmediateCommandList::MakeState(rhi::ResourceAccessType access) const {
        // Match what PassBuilders do (render vs compute sync selection).
        return ResourceState{
            access,
            AccessToLayout(access, /*isRender=*/m_isRenderPass),
            m_isRenderPass ? RenderSyncFromAccess(access) : ComputeSyncFromAccess(access)
        };
    }

    void ImmediateCommandList::Track(Resource* pRes, uint64_t rid, RangeSpec range, rhi::ResourceAccessType access) {
        ResourceState want = MakeState(access);

        // if a previously-recorded command forced this same range into a different non-Common state, treat that as an error 
        // TODO: Allow internal transitions in immediate passes
        auto& tr = m_trackers.at(rid);
        std::vector<ResourceTransition> tmp;
        tr.Apply(range, pRes, want, tmp);

        for (auto const& t : tmp) {
            // If we're transitioning from something other than Common, we have a multi-state requirement.
            // (v1: disallow, to keep scheduling model simple)
            if (t.prevAccessType != rhi::ResourceAccessType::Common &&
                t.prevAccessType != t.newAccessType) {
                throw std::runtime_error("ImmediateCommandList: conflicting access states within one pass (needs internal barriers)");
            }
        }
    }

    void ImmediateCommandList::CopyBufferRegion(Resolved const& dst, uint64_t dstOffset,
        Resolved const& src, uint64_t srcOffset,
        uint64_t numBytes) {
        if (!m_dispatch.GetResourceHandle) {
            throw std::runtime_error("ImmediateDispatch::GetResourceHandle not set");
        }

        CopyBufferRegionCmd cmd;
        cmd.dst = m_dispatch.GetResourceHandle(*dst.raw);
        cmd.dstOffset = dstOffset;
        cmd.src = m_dispatch.GetResourceHandle(*src.raw);
        cmd.srcOffset = srcOffset;
        cmd.numBytes = numBytes;

        m_writer.WriteOp(Op::CopyBufferRegion);
        m_writer.WritePOD(cmd);

        RangeSpec whole{};
        Track(dst.raw, dst.globalId, whole, rhi::ResourceAccessType::CopyDest);
        Track(src.raw, src.globalId, whole, rhi::ResourceAccessType::CopySource);
    }

    void ImmediateCommandList::ClearRTV(Resolved const& target, float r, float g, float b, float a, RangeSpec range)
    {
        if (!m_dispatch.GetRTV) {
            throw std::runtime_error("ImmediateDispatch::GetRTV not set");
        }

        rhi::ClearValue cv{};
        cv.type = rhi::ClearValueType::Color;
        cv.rgba[0] = r;
        cv.rgba[1] = g;
        cv.rgba[2] = b;
        cv.rgba[3] = a;

        const bool any = ForEachMipSlice(*target.raw, range,
            [&](uint32_t /*mip*/, uint32_t /*slice*/, RangeSpec exact)
            {
                const rhi::DescriptorSlot rtv = m_dispatch.GetRTV(*target.raw, exact);
                RequireValidSlot(rtv, "RTV");

                ClearRTVCmd cmd{};
                cmd.rtv = rtv;
                cmd.clear = cv;

                m_writer.WriteOp(Op::ClearRTV);
                m_writer.WritePOD(cmd);
            });

        if (any) {
            Track(target.raw, target.globalId, range, rhi::ResourceAccessType::RenderTarget);
        }
    }

    void ImmediateCommandList::ClearDSV(Resolved const& target,
        bool clearDepth, float depth,
        bool clearStencil, uint8_t stencil,
        RangeSpec range)
    {
        if (!clearDepth && !clearStencil) {
            return;
        }

        if (!m_dispatch.GetDSV) {
            throw std::runtime_error("ImmediateDispatch::GetDSV not set");
        }

        const bool any = ForEachMipSlice(*target.raw, range,
            [&](uint32_t /*mip*/, uint32_t /*slice*/, RangeSpec exact)
            {
                const rhi::DescriptorSlot dsv = m_dispatch.GetDSV(*target.raw, exact);
                RequireValidSlot(dsv, "DSV");

                ClearDSVCmd cmd{};
                cmd.dsv = dsv;
                cmd.clearDepth = clearDepth;
                cmd.clearStencil = clearStencil;
                cmd.depth = depth;
                cmd.stencil = stencil;

                m_writer.WriteOp(Op::ClearDSV);
                m_writer.WritePOD(cmd);
            });

        if (any) {
            Track(target.raw, target.globalId, range, rhi::ResourceAccessType::DepthReadWrite);
        }
    }

    void ImmediateCommandList::ClearUavFloat(Resolved const& target, float x, float y, float z, float w, RangeSpec range)
    {
        if (!m_dispatch.GetUavClearInfo) {
            throw std::runtime_error("ImmediateDispatch::GetUavClearInfo not set");
        }

        rhi::UavClearFloat value{};
        value.v[0] = x; value.v[1] = y; value.v[2] = z; value.v[3] = w;

        const bool any = ForEachMipSlice(*target.raw, range,
            [&](uint32_t /*mip*/, uint32_t /*slice*/, RangeSpec exact)
            {
                ClearUavFloatCmd cmd{};
                cmd.value = value;

                if (!m_dispatch.GetUavClearInfo(*target.raw, exact, cmd.info)) {
                    throw std::runtime_error("Immediate clear: GetUavClearInfo failed");
                }

                if (!cmd.info.shaderVisible.heap.valid() || !cmd.info.cpuVisible.heap.valid()) {
                    throw std::runtime_error("Immediate clear: invalid UAV descriptor slots");
                }

                m_writer.WriteOp(Op::ClearUavFloat);
                m_writer.WritePOD(cmd);
            });

        if (any)
            Track(target.raw, target.globalId, range, rhi::ResourceAccessType::UnorderedAccess);
    }

    void ImmediateCommandList::ClearUavUint(Resolved const& target, uint32_t x, uint32_t y, uint32_t z, uint32_t w, RangeSpec range)
    {
        if (!m_dispatch.GetUavClearInfo) {
            throw std::runtime_error("ImmediateDispatch::GetUavClearInfo not set");
        }

        rhi::UavClearUint value{};
        value.v[0] = x; value.v[1] = y; value.v[2] = z; value.v[3] = w;

        const bool any = ForEachMipSlice(*target.raw, range,
            [&](uint32_t /*mip*/, uint32_t /*slice*/, RangeSpec exact)
            {
                ClearUavUintCmd cmd{};
                cmd.value = value;

                if (!m_dispatch.GetUavClearInfo(*target.raw, exact, cmd.info)) {
                    throw std::runtime_error("Immediate clear: GetUavClearInfo failed");
                }

                if (!cmd.info.shaderVisible.heap.valid() || !cmd.info.cpuVisible.heap.valid()) {
                    throw std::runtime_error("Immediate clear: invalid UAV descriptor slots");
                }

                m_writer.WriteOp(Op::ClearUavUint);
                m_writer.WritePOD(cmd);
            });

        if (any)
            Track(target.raw, target.globalId, range, rhi::ResourceAccessType::UnorderedAccess);
    }

    void ImmediateCommandList::CopyTextureRegion(
        Resolved const& dst, uint32_t dstMip, uint32_t dstSlice, uint32_t dstX, uint32_t dstY, uint32_t dstZ,
        Resolved const& src, uint32_t srcMip, uint32_t srcSlice, uint32_t srcX, uint32_t srcY, uint32_t srcZ,
        uint32_t width, uint32_t height, uint32_t depth)
    {
        if (!m_dispatch.GetResourceHandle) {
            throw std::runtime_error("ImmediateDispatch::GetResourceHandle not set");
        }

        CopyTextureRegionCmd cmd{};
        cmd.dst.texture = m_dispatch.GetResourceHandle(*dst.raw);
        cmd.dst.mip = dstMip;
        cmd.dst.arraySlice = dstSlice;
        cmd.dst.x = dstX; cmd.dst.y = dstY; cmd.dst.z = dstZ;
        cmd.dst.width = width;
        cmd.dst.height = height;
        cmd.dst.depth = depth;

        cmd.src.texture = m_dispatch.GetResourceHandle(*src.raw);
        cmd.src.mip = srcMip;
        cmd.src.arraySlice = srcSlice;
        cmd.src.x = srcX; cmd.src.y = srcY; cmd.src.z = srcZ;
        cmd.src.width = width;
        cmd.src.height = height;
        cmd.src.depth = depth;

        m_writer.WriteOp(Op::CopyTextureRegion);
        m_writer.WritePOD(cmd);

        Track(dst.raw, dst.globalId, MakeExactMipSlice(dstMip, dstSlice), rhi::ResourceAccessType::CopyDest);
        Track(src.raw, src.globalId, MakeExactMipSlice(srcMip, srcSlice), rhi::ResourceAccessType::CopySource);
    }

    void ImmediateCommandList::CopyTextureToBuffer(
        Resolved const& texture, uint32_t mip, uint32_t slice,
        Resolved const& buffer,
        rhi::CopyableFootprint const& footprint,
        uint32_t x, uint32_t y, uint32_t z)
    {
        if (!m_dispatch.GetResourceHandle) {
            throw std::runtime_error("ImmediateDispatch::GetResourceHandle not set");
        }

        CopyTextureToBufferCmd cmd{};
        cmd.region.texture = m_dispatch.GetResourceHandle(*texture.raw);
        cmd.region.buffer = m_dispatch.GetResourceHandle(*buffer.raw);
        cmd.region.mip = mip;
        cmd.region.arraySlice = slice;
        cmd.region.x = x; cmd.region.y = y; cmd.region.z = z;
        cmd.region.footprint = footprint;

        m_writer.WriteOp(Op::CopyTextureToBuffer);
        m_writer.WritePOD(cmd);

        Track(texture.raw, texture.globalId, MakeExactMipSlice(mip, slice), rhi::ResourceAccessType::CopySource);
        RangeSpec whole{};
        Track(buffer.raw, buffer.globalId, whole, rhi::ResourceAccessType::CopyDest);
    }

    void ImmediateCommandList::CopyBufferToTexture(
        Resolved const& buffer,
        Resolved const& texture, uint32_t mip, uint32_t slice,
        rhi::CopyableFootprint const& footprint,
        uint32_t x, uint32_t y, uint32_t z)
    {
        if (!m_dispatch.GetResourceHandle) {
            throw std::runtime_error("ImmediateDispatch::GetResourceHandle not set");
        }

        CopyBufferToTextureCmd cmd{};
        cmd.region.texture = m_dispatch.GetResourceHandle(*texture.raw);
        cmd.region.buffer = m_dispatch.GetResourceHandle(*buffer.raw);
        cmd.region.mip = mip;
        cmd.region.arraySlice = slice;
        cmd.region.x = x; cmd.region.y = y; cmd.region.z = z;
        cmd.region.footprint = footprint;

        m_writer.WriteOp(Op::CopyBufferToTexture);
        m_writer.WritePOD(cmd);

        RangeSpec whole{};
        Track(buffer.raw, buffer.globalId, whole, rhi::ResourceAccessType::CopySource);
        Track(texture.raw, texture.globalId, MakeExactMipSlice(mip, slice), rhi::ResourceAccessType::CopyDest);
    }


} // namespace rg::imm