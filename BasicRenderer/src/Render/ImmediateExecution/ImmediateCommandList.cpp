#include "Render/ImmediateExecution/ImmediateCommandList.h"

#include "Render/ResourceRegistry.h"

namespace rg::imm {

    namespace {

        using Interval = rg::imm::ImmediateCommandList::SliceInterval;

        static inline void InsertAndUnionInterval(std::vector<Interval>& v, uint32_t lo, uint32_t hi)
        {
            if (lo > hi) return;

            // v is kept sorted and disjoint, inclusive.
            auto it = std::lower_bound(v.begin(), v.end(), lo,
                [](const Interval& a, uint32_t value) {
                    // a is strictly before value if it ends before value-1
                    return (a.hi + 1) < value;
                });

            uint32_t newLo = lo;
            uint32_t newHi = hi;

            while (it != v.end() && it->lo <= (newHi + 1)) {
                newLo = std::min(newLo, it->lo);
                newHi = std::max(newHi, it->hi);
                it = v.erase(it);
            }

            v.insert(it, Interval{ newLo, newHi });
        }

        struct Rect {
            uint32_t mip0 = 0, mip1 = 0;     // inclusive
            uint32_t slice0 = 0, slice1 = 0; // inclusive
        };

        static inline bool TouchOrOverlap1D(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) {
            // inclusive, touch counts
            return (a1 + 1 >= b0) && (b1 + 1 >= a0);
        }

        static inline bool TryMergeRect(Rect& a, const Rect& b)
        {
            // Merge along mip axis (same slice span)
            if (a.slice0 == b.slice0 && a.slice1 == b.slice1 &&
                TouchOrOverlap1D(a.mip0, a.mip1, b.mip0, b.mip1))
            {
                a.mip0 = std::min(a.mip0, b.mip0);
                a.mip1 = std::max(a.mip1, b.mip1);
                return true;
            }

            // Merge along slice axis (same mip span)
            if (a.mip0 == b.mip0 && a.mip1 == b.mip1 &&
                TouchOrOverlap1D(a.slice0, a.slice1, b.slice0, b.slice1))
            {
                a.slice0 = std::min(a.slice0, b.slice0);
                a.slice1 = std::max(a.slice1, b.slice1);
                return true;
            }

            return false;
        }

        static inline void MergeRectsUntilStable(std::vector<Rect>& rects)
        {
            if (rects.size() <= 1) return;

            bool changed = false;
            do {
                changed = false;

                std::sort(rects.begin(), rects.end(),
                    [](const Rect& L, const Rect& R) {
                        // Sort by slice span, then mip span (keeps likely-merge neighbors adjacent)
                        if (L.slice0 != R.slice0) return L.slice0 < R.slice0;
                        if (L.slice1 != R.slice1) return L.slice1 < R.slice1;
                        if (L.mip0 != R.mip0) return L.mip0 < R.mip0;
                        return L.mip1 < R.mip1;
                    });

                std::vector<Rect> out;
                out.reserve(rects.size());

                for (const Rect& r : rects) {
                    if (!out.empty() && TryMergeRect(out.back(), r)) {
                        changed = true;
                    }
                    else {
                        out.push_back(r);
                    }
                }

                rects.swap(out);
            } while (changed);
        }

        static inline RangeSpec RectToRangeSpec(const Rect& r, uint32_t totalMips, uint32_t totalSlices)
        {
            RangeSpec out{};

            // mips
            if (r.mip0 == 0 && r.mip1 == totalMips - 1) {
                out.mipLower = { BoundType::All, 0 };
                out.mipUpper = { BoundType::All, 0 };
            }
            else if (r.mip0 == r.mip1) {
                out.mipLower = { BoundType::Exact, r.mip0 };
                out.mipUpper = { BoundType::Exact, r.mip1 };
            }
            else {
                out.mipLower = { BoundType::From, r.mip0 };
                out.mipUpper = { BoundType::UpTo, r.mip1 };
            }

            // slices
            if (r.slice0 == 0 && r.slice1 == totalSlices - 1) {
                out.sliceLower = { BoundType::All, 0 };
                out.sliceUpper = { BoundType::All, 0 };
            }
            else if (r.slice0 == r.slice1) {
                out.sliceLower = { BoundType::Exact, r.slice0 };
                out.sliceUpper = { BoundType::Exact, r.slice1 };
            }
            else {
                out.sliceLower = { BoundType::From, r.slice0 };
                out.sliceUpper = { BoundType::UpTo, r.slice1 };
            }

            return out;
        }

    } // anonymous namespace


	// BytecodeReader functions
	bool BytecodeReader::Empty() const noexcept { return cur >= end; }

    Op BytecodeReader::ReadOp() {
        Require(1);
        Op op = static_cast<Op>(*cur);
        cur += 1;
        return op;
    }

    void BytecodeReader::Require(const size_t n) const {
        if (cur + n > end) {
            throw std::runtime_error("Immediate bytecode underflow");
        }
    }

    void BytecodeReader::Align(const size_t a) {
        if (a == 0) return;
        const uintptr_t ip = reinterpret_cast<uintptr_t>(cur);
        const uintptr_t aligned = (ip + (a - 1)) & ~(a - 1);
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
        m_handles.clear();
        m_access.clear();
    }

    FrameData ImmediateCommandList::Finalize() {
        FrameData out;
        out.bytecode = m_writer.data;
        out.keepAlive = std::move(m_keepAlive);

        out.requirements.reserve(64);

        for (auto& [rid, acc] : m_access)
        {
            if (!acc.hasState)
                continue;

            auto itH = m_handles.find(rid);
            if (itH == m_handles.end())
                continue;

            // Build rectangles by extending identical slice-intervals across consecutive mips.
            std::unordered_map<uint64_t, Rect> open; // key=(sl0,sl1)
            open.reserve(64);

            std::vector<Rect> rects;
            rects.reserve(64);

            auto keyOf = [](uint32_t lo, uint32_t hi) -> uint64_t {
                return (uint64_t(lo) << 32) | uint64_t(hi);
                };

            // Track which keys were seen this mip so we can close the rest.
            std::unordered_map<uint64_t, uint32_t> lastSeen;
            lastSeen.reserve(64);

            for (uint32_t mip = 0; mip < acc.totalMips; ++mip)
            {
                // mark seen this mip
                for (const auto& in : acc.perMip[mip])
                {
                    const uint64_t k = keyOf(in.lo, in.hi);
                    lastSeen[k] = mip;

                    auto it = open.find(k);
                    if (it == open.end()) {
                        open.emplace(k, Rect{ mip, mip, in.lo, in.hi });
                    }
                    else {
                        // if it was open, extend only if consecutive; otherwise close & restart
                        if (it->second.mip1 + 1 == mip) {
                            it->second.mip1 = mip;
                        }
                        else {
                            rects.push_back(it->second);
                            it->second = Rect{ mip, mip, in.lo, in.hi };
                        }
                    }
                }

                // close any open rects not seen on this mip
                for (auto it = open.begin(); it != open.end(); )
                {
                    auto ls = lastSeen.find(it->first);
                    if (ls == lastSeen.end() || ls->second != mip) {
                        rects.push_back(it->second);
                        it = open.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }

            // close remaining
            for (auto& [k, r] : open) {
                (void)k;
                rects.push_back(r);
            }

            // Merge rectangles across axes where it does not introduce unused coverage.
            MergeRectsUntilStable(rects);

            // Emit requirements.
            for (const Rect& r : rects)
            {
                ResourceRequirement rr{ itH->second };
                rr.resourceHandleAndRange.range = RectToRangeSpec(r, acc.totalMips, acc.totalSlices);
                rr.state = acc.state;
                out.requirements.push_back(rr);
            }
        }

        return out;
    }

    ImmediateCommandList::Resolved ImmediateCommandList::Resolve(ResourceIdentifier const& id) {
        if (!m_resolveByIdFn) throw std::runtime_error("ImmediateCommandList has no ResolveByIdFn");
        const auto handle = m_resolveByIdFn(m_resolveUser, id, /*allowFailure=*/false);

        const uint64_t rid = handle.GetGlobalResourceID();
        m_handles[rid] = handle; // used in Finalize()
        return { handle };
    }

    ImmediateCommandList::Resolved ImmediateCommandList::Resolve(Resource* p) {
        if (!p) throw std::runtime_error("ImmediateCommandList: null Resource*");
        const auto handle = m_resolveByPtrFn(m_resolveUser, p, /*allowFailure=*/false);

        const uint64_t rid = handle.GetGlobalResourceID();
        m_handles[rid] = handle;
        return { handle };
    }

    ImmediateCommandList::Resolved ImmediateCommandList::Resolve(Resource* p, const std::shared_ptr<Resource>& keepAlive) {
        if (!p) throw std::runtime_error("ImmediateCommandList: null Resource*");

        auto handle = ResourceRegistry::RegistryHandle::MakeEphemeral(p);
        const uint64_t rid = handle.GetGlobalResourceID();
        m_handles[rid] = handle;

        if (keepAlive) {
            m_keepAlive->pinShared(keepAlive);
        }

        return { handle };
    }

    ResourceState ImmediateCommandList::MakeState(const rhi::ResourceAccessType access) const {
        // Match what PassBuilders do (render vs compute sync selection).
        return ResourceState{
            access,
            AccessToLayout(access, /*isRender=*/m_isRenderPass),
            m_isRenderPass ? RenderSyncFromAccess(access) : ComputeSyncFromAccess(access)
        };
    }

    void ImmediateCommandList::Track(ResourceRegistry::RegistryHandle handle, const uint64_t rid, const RangeSpec& range, const rhi::ResourceAccessType access) {
        const ResourceState want = MakeState(access);

        // Resolve dims now (needed for exact marking / compression).
        uint32_t totalMips = handle.GetNumMipLevels();
        uint32_t totalSlices = handle.GetArraySize();
        if (totalMips == 0) totalMips = 1;
        if (totalSlices == 0) totalSlices = 1;

        SubresourceRange sr = ResolveRangeSpec(range, totalMips, totalSlices);
        if (sr.isEmpty())
            return; // ignore empty regions

        auto& acc = m_access[rid];
        acc.EnsureDims(totalMips, totalSlices);

        if (!acc.hasState) {
            acc.hasState = true;
            acc.state = want;
        }
        else {
            // Disallow multi-state within the same immediate list
			// TODO: Allow with internal barriers?
            if (!(acc.state == want)) {
                throw std::runtime_error(
                    "ImmediateCommandList: conflicting access states within one pass (needs internal barriers)");
            }
        }

        // Mark union-of-touched (exact, no unused coverage).
        const uint32_t mip0 = sr.firstMip;
        const uint32_t mip1 = sr.firstMip + sr.mipCount - 1;
        const uint32_t sl0 = sr.firstSlice;
        const uint32_t sl1 = sr.firstSlice + sr.sliceCount - 1;

        for (uint32_t mip = mip0; mip <= mip1; ++mip) {
            InsertAndUnionInterval(acc.perMip[mip], sl0, sl1);
        }
    }

    void ImmediateCommandList::CopyBufferRegion(Resolved const& dst, const uint64_t dstOffset,
        Resolved const& src, const uint64_t srcOffset,
        const uint64_t numBytes) {
        if (!m_dispatch.GetResourceHandle) {
            throw std::runtime_error("ImmediateDispatch::GetResourceHandle not set");
        }

        CopyBufferRegionCmd cmd;
        cmd.dst = m_dispatch.GetResourceHandle(m_dispatch.user, dst.handle);
        cmd.dstOffset = dstOffset;
        cmd.src = m_dispatch.GetResourceHandle(m_dispatch.user, src.handle);
        cmd.srcOffset = srcOffset;
        cmd.numBytes = numBytes;

        m_writer.WriteOp(Op::CopyBufferRegion);
        m_writer.WritePOD(cmd);

        RangeSpec whole{};
        Track(dst.handle, dst.handle.GetGlobalResourceID(), whole, rhi::ResourceAccessType::CopyDest);
        Track(src.handle, src.handle.GetGlobalResourceID(), whole, rhi::ResourceAccessType::CopySource);
    }

    void ImmediateCommandList::ClearRTV(Resolved const& target, const float r, const float g, const float b, const float a, const RangeSpec& range)
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

        const bool any = ForEachMipSlice(target.handle, range,
            [&](uint32_t /*mip*/, uint32_t /*slice*/, const RangeSpec& exact)
            {
                const rhi::DescriptorSlot rtv = m_dispatch.GetRTV(m_dispatch.user, target.handle, exact);
                RequireValidSlot(rtv, "RTV");

                ClearRTVCmd cmd{};
                cmd.rtv = rtv;
                cmd.clear = cv;

                m_writer.WriteOp(Op::ClearRTV);
                m_writer.WritePOD(cmd);
            });

        if (any) {
            Track(target.handle, target.handle.GetGlobalResourceID(), range, rhi::ResourceAccessType::RenderTarget);
        }
    }

    void ImmediateCommandList::ClearDSV(Resolved const& target,
        bool clearDepth, float depth,
        bool clearStencil, uint8_t stencil,
        const RangeSpec& range)
    {
        if (!clearDepth && !clearStencil) {
            return;
        }

        if (!m_dispatch.GetDSV) {
            throw std::runtime_error("ImmediateDispatch::GetDSV not set");
        }

        const bool any = ForEachMipSlice(target.handle, range,
            [&](uint32_t /*mip*/, uint32_t /*slice*/, const RangeSpec& exact)
            {
                const rhi::DescriptorSlot dsv = m_dispatch.GetDSV(m_dispatch.user, target.handle, exact);
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
            Track(target.handle, target.handle.GetGlobalResourceID(), range, rhi::ResourceAccessType::DepthReadWrite);
        }
    }

    void ImmediateCommandList::ClearUavFloat(Resolved const& target, const float x, const float y, const float z, const float w, const RangeSpec& range)
    {
        if (!m_dispatch.GetUavClearInfo) {
            throw std::runtime_error("ImmediateDispatch::GetUavClearInfo not set");
        }

        rhi::UavClearFloat value{};
        value.v[0] = x; value.v[1] = y; value.v[2] = z; value.v[3] = w;

        const bool any = ForEachMipSlice(target.handle, range,
            [&](uint32_t /*mip*/, uint32_t /*slice*/, const RangeSpec& exact)
            {
                ClearUavFloatCmd cmd{};
                cmd.value = value;

                if (!m_dispatch.GetUavClearInfo(m_dispatch.user, target.handle, exact, cmd.info)) {
                    throw std::runtime_error("Immediate clear: GetUavClearInfo failed");
                }

                if (!cmd.info.shaderVisible.heap.valid() || !cmd.info.cpuVisible.heap.valid()) {
                    throw std::runtime_error("Immediate clear: invalid UAV descriptor slots");
                }

                m_writer.WriteOp(Op::ClearUavFloat);
                m_writer.WritePOD(cmd);
            });

        if (any)
            Track(target.handle, target.handle.GetGlobalResourceID(), range, rhi::ResourceAccessType::UnorderedAccess);
    }

    void ImmediateCommandList::ClearUavUint(Resolved const& target, const uint32_t x, const uint32_t y, const uint32_t z, const uint32_t w, const RangeSpec& range)
    {
        if (!m_dispatch.GetUavClearInfo) {
            throw std::runtime_error("ImmediateDispatch::GetUavClearInfo not set");
        }

        rhi::UavClearUint value{};
        value.v[0] = x; value.v[1] = y; value.v[2] = z; value.v[3] = w;

        const bool any = ForEachMipSlice(target.handle, range,
            [&](uint32_t /*mip*/, uint32_t /*slice*/, const RangeSpec& exact)
            {
                ClearUavUintCmd cmd{};
                cmd.value = value;

                if (!m_dispatch.GetUavClearInfo(m_dispatch.user, target.handle, exact, cmd.info)) {
                    throw std::runtime_error("Immediate clear: GetUavClearInfo failed");
                }

                if (!cmd.info.shaderVisible.heap.valid() || !cmd.info.cpuVisible.heap.valid()) {
                    throw std::runtime_error("Immediate clear: invalid UAV descriptor slots");
                }

                m_writer.WriteOp(Op::ClearUavUint);
                m_writer.WritePOD(cmd);
            });

        if (any)
            Track(target.handle, target.handle.GetGlobalResourceID(), range, rhi::ResourceAccessType::UnorderedAccess);
    }

    void ImmediateCommandList::CopyTextureRegion(
        Resolved const& dst, const uint32_t dstMip, const uint32_t dstSlice, const uint32_t dstX, const uint32_t dstY, const uint32_t dstZ,
        Resolved const& src, const uint32_t srcMip, const uint32_t srcSlice, const uint32_t srcX, const uint32_t srcY, const uint32_t srcZ,
        const uint32_t width, const uint32_t height, const uint32_t depth)
    {
        if (!m_dispatch.GetResourceHandle) {
            throw std::runtime_error("ImmediateDispatch::GetResourceHandle not set");
        }

        CopyTextureRegionCmd cmd{};
        cmd.dst.texture = m_dispatch.GetResourceHandle(m_dispatch.user, dst.handle);
        cmd.dst.mip = dstMip;
        cmd.dst.arraySlice = dstSlice;
        cmd.dst.x = dstX; cmd.dst.y = dstY; cmd.dst.z = dstZ;
        cmd.dst.width = width;
        cmd.dst.height = height;
        cmd.dst.depth = depth;

        cmd.src.texture = m_dispatch.GetResourceHandle(m_dispatch.user, src.handle);
        cmd.src.mip = srcMip;
        cmd.src.arraySlice = srcSlice;
        cmd.src.x = srcX; cmd.src.y = srcY; cmd.src.z = srcZ;
        cmd.src.width = width;
        cmd.src.height = height;
        cmd.src.depth = depth;

        m_writer.WriteOp(Op::CopyTextureRegion);
        m_writer.WritePOD(cmd);

        Track(dst.handle, dst.handle.GetGlobalResourceID(), MakeExactMipSlice(dstMip, dstSlice), rhi::ResourceAccessType::CopyDest);
        Track(src.handle, src.handle.GetGlobalResourceID(), MakeExactMipSlice(srcMip, srcSlice), rhi::ResourceAccessType::CopySource);
    }

    void ImmediateCommandList::CopyTextureToBuffer(
        Resolved const& texture, const uint32_t mip, const uint32_t slice,
        Resolved const& buffer,
        rhi::CopyableFootprint const& footprint,
        const uint32_t x, const uint32_t y, const uint32_t z)
    {
        if (!m_dispatch.GetResourceHandle) {
            throw std::runtime_error("ImmediateDispatch::GetResourceHandle not set");
        }

        CopyTextureToBufferCmd cmd{};
        cmd.region.texture = m_dispatch.GetResourceHandle(m_dispatch.user, texture.handle);
        cmd.region.buffer = m_dispatch.GetResourceHandle(m_dispatch.user, buffer.handle);
        cmd.region.mip = mip;
        cmd.region.arraySlice = slice;
        cmd.region.x = x; cmd.region.y = y; cmd.region.z = z;
        cmd.region.footprint = footprint;

        m_writer.WriteOp(Op::CopyTextureToBuffer);
        m_writer.WritePOD(cmd);

        Track(texture.handle, texture.handle.GetGlobalResourceID(), MakeExactMipSlice(mip, slice), rhi::ResourceAccessType::CopySource);
        RangeSpec whole{};
        Track(buffer.handle, buffer.handle.GetGlobalResourceID(), whole, rhi::ResourceAccessType::CopyDest);
    }

    void ImmediateCommandList::CopyBufferToTexture(
        Resolved const& buffer,
        Resolved const& texture, const uint32_t mip, const uint32_t slice,
        rhi::CopyableFootprint const& footprint,
        const uint32_t x, const uint32_t y, const uint32_t z)
    {
        if (!m_dispatch.GetResourceHandle) {
            throw std::runtime_error("ImmediateDispatch::GetResourceHandle not set");
        }

        CopyBufferToTextureCmd cmd{};
        cmd.region.texture = m_dispatch.GetResourceHandle(m_dispatch.user, texture.handle);
        cmd.region.buffer = m_dispatch.GetResourceHandle(m_dispatch.user, buffer.handle);
        cmd.region.mip = mip;
        cmd.region.arraySlice = slice;
        cmd.region.x = x; cmd.region.y = y; cmd.region.z = z;
        cmd.region.footprint = footprint;

        m_writer.WriteOp(Op::CopyBufferToTexture);
        m_writer.WritePOD(cmd);

        RangeSpec whole{};
        Track(buffer.handle, buffer.handle.GetGlobalResourceID(), whole, rhi::ResourceAccessType::CopySource);
        Track(texture.handle, texture.handle.GetGlobalResourceID(), MakeExactMipSlice(mip, slice), rhi::ResourceAccessType::CopyDest);
    }
} // namespace rg::imm