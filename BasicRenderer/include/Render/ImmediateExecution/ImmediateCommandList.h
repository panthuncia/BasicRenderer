#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <type_traits>
#include <cstring>
#include <stdexcept>

#include <rhi.h>

#include "Resources/ResourceIdentifier.h"
#include "Resources/ResourceStateTracker.h"   // RangeSpec, SymbolicTracker, ResourceState
#include "Render/ResourceRequirements.h"      // ResourceRequirement, ResourceAndRange
#include "Resources/Resource.h"
#include "Resources/GloballyIndexedResource.h"

namespace rg::imm {

    // RenderGraph provides this thunk so the immediate list can resolve identifiers
    // without going through the pass's restricted registry view.
    using ResolveByIdFn = std::shared_ptr<Resource>(*)(void* user, ResourceIdentifier const& id, bool allowFailure);

    // "Dispatch" that lives on RenderGraph so immediate recording can
    // turn a Resource into low-level RHI handles/descriptor slots at record time.
    // Replay then needs only the RHI command list + bytecode stream.

    struct ImmediateDispatch {
        rhi::ResourceHandle(*GetResourceHandle)(Resource& r) noexcept = nullptr;

        // These expect RangeSpec that resolves to (at least) one mip/slice.
        rhi::DescriptorSlot(*GetRTV)(Resource& r, RangeSpec range) noexcept = nullptr;
        rhi::DescriptorSlot(*GetDSV)(Resource& r, RangeSpec range) noexcept = nullptr;

        // Returns false if the resource can't provide the required UAV clear info.
        bool (*GetUavClearInfo)(Resource& r, RangeSpec range, rhi::UavClearInfo& out) noexcept = nullptr;
    };

    inline bool ResolveFirstMipSlice(Resource& r, RangeSpec range, uint32_t& outMip, uint32_t& outSlice) noexcept
    {
        const uint32_t totalMips = r.GetMipLevels();
        const uint32_t totalSlices = r.GetArraySize();
        if (totalMips == 0 || totalSlices == 0) return false;

        SubresourceRange sr = ResolveRangeSpec(range, totalMips, totalSlices);
        if (sr.isEmpty()) return false;

        outMip = sr.firstMip;
        outSlice = sr.firstSlice;
        return true;
    }

    inline ImmediateDispatch MakeDefaultImmediateDispatch() noexcept
    {
        ImmediateDispatch d{};

        d.GetResourceHandle = +[](Resource& r) noexcept -> rhi::ResourceHandle {
            return r.GetAPIResource().GetHandle();
            };

        d.GetRTV = +[](Resource& r, RangeSpec range) noexcept -> rhi::DescriptorSlot {
            auto* gir = dynamic_cast<GloballyIndexedResource*>(&r);
            if (!gir || !gir->HasRTV()) return {};

            uint32_t mip = 0, slice = 0;
            if (!ResolveFirstMipSlice(r, range, mip, slice)) return {};

            return gir->GetRTVInfo(mip, slice).slot;
            };

        d.GetDSV = +[](Resource& r, RangeSpec range) noexcept -> rhi::DescriptorSlot {
            auto* gir = dynamic_cast<GloballyIndexedResource*>(&r);
            if (!gir || !gir->HasDSV()) return {};

            uint32_t mip = 0, slice = 0;
            if (!ResolveFirstMipSlice(r, range, mip, slice)) return {};

            return gir->GetDSVInfo(mip, slice).slot;
            };

        d.GetUavClearInfo = +[](Resource& r, RangeSpec range, rhi::UavClearInfo& out) noexcept -> bool {
            auto* gir = dynamic_cast<GloballyIndexedResource*>(&r);

            // DX12 path requires both a shader-visible and CPU-visible UAV descriptor.
            if (!gir || !gir->HasUAVShaderVisible() || !gir->HasUAVNonShaderVisible()) return false;

            uint32_t mip = 0, slice = 0;
            if (!ResolveFirstMipSlice(r, range, mip, slice)) return false;

            out.shaderVisible = gir->GetUAVShaderVisibleInfo(mip, slice).slot;
            out.cpuVisible = gir->GetUAVNonShaderVisibleInfo(mip, slice).slot;

            out.resource = r.GetAPIResource();

            return true;
            };

        return d;
    }


    enum class Op : uint8_t {
        CopyBufferRegion = 1,
        ClearRTV = 2,
        ClearDSV = 3,
        ClearUavFloat = 4,
        // TODO: ClearUavUint, CopyTextureRegion, etc.
    };

    struct CopyBufferRegionCmd {
        rhi::ResourceHandle dst{};
        uint64_t dstOffset = 0;
        rhi::ResourceHandle src{};
        uint64_t srcOffset = 0;
        uint64_t numBytes = 0;
    };

    struct ClearRTVCmd {
        rhi::DescriptorSlot rtv{};
        rhi::ClearValue clear{};
    };

    struct ClearDSVCmd {
        rhi::DescriptorSlot dsv{};
        bool clearDepth = true;
        bool clearStencil = false;
        float depth = 1.0f;
        uint8_t stencil = 0;
    };

    struct ClearUavFloatCmd {
        rhi::UavClearInfo  info{};
        rhi::UavClearFloat value{};
    };

    // Simple aligned POD writer/reader for a bytecode stream.
    class BytecodeWriter {
    public:
        std::vector<std::byte> data;

        void Reset() { data.clear(); }

        void WriteOp(Op op) {
            data.push_back(static_cast<std::byte>(op));
        }

        template<class T>
        void WritePOD(T const& v) {
            static_assert(std::is_trivially_copyable_v<T>);
            Align(alignof(T));
            size_t old = data.size();
            data.resize(old + sizeof(T));
            std::memcpy(data.data() + old, &v, sizeof(T));
        }

    private:
        void Align(size_t a) {
            size_t cur = data.size();
            size_t pad = (a == 0) ? 0 : ((a - (cur % a)) % a);
            if (pad) {
                data.insert(data.end(), pad, std::byte{ 0 });
            }
        }
    };

    class BytecodeReader {
    public:
        BytecodeReader(std::byte const* p, size_t n) : base(p), cur(p), end(p + n) {}

        bool Empty() const noexcept { return cur >= end; }

        Op ReadOp() {
            Require(1);
            Op op = static_cast<Op>(*cur);
            cur += 1;
            return op;
        }

        template<class T>
        T ReadPOD() {
            static_assert(std::is_trivially_copyable_v<T>);
            Align(alignof(T));
            Require(sizeof(T));
            T out{};
            std::memcpy(&out, cur, sizeof(T));
            cur += sizeof(T);
            return out;
        }

    private:
        void Require(size_t n) const {
            if (cur + n > end) {
                throw std::runtime_error("Immediate bytecode underflow");
            }
        }
        void Align(size_t a) {
            if (a == 0) return;
            uintptr_t ip = reinterpret_cast<uintptr_t>(cur);
            uintptr_t aligned = (ip + (a - 1)) & ~(uintptr_t(a - 1));
            cur = reinterpret_cast<std::byte const*>(aligned);
        }

        std::byte const* base = nullptr;
        std::byte const* cur = nullptr;
        std::byte const* end = nullptr;
    };

    // Replay bytecode into a concrete RHI command list.
    inline void Replay(std::vector<std::byte> const& bytecode, rhi::CommandList& cl) {
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
            default:
                throw std::runtime_error("Unknown immediate bytecode op");
            }
        }
    }

    struct FrameData {
        std::vector<std::byte> bytecode;                 // replay payload
        std::vector<ResourceRequirement> requirements;   // merged segments
        void Reset() { bytecode.clear(); requirements.clear(); }
    };

    // Immediate command list: records bytecode + tracks resource access requirements.
    class ImmediateCommandList {
    public:
        ImmediateCommandList(bool isRenderPass,
            ImmediateDispatch const& dispatch,
            ResolveByIdFn resolveFn,
            void* resolveUser)
            : m_isRenderPass(isRenderPass)
            , m_dispatch(dispatch)
            , m_resolveFn(resolveFn)
            , m_resolveUser(resolveUser)
        {
        }

        void Reset() {
            m_writer.Reset();
            m_ptrs.clear();
            m_trackers.clear();
        }

        // API: resources can be ResourceIdentifier or Resource*

        void CopyBufferRegion(ResourceIdentifier const& dst, uint64_t dstOffset,
            ResourceIdentifier const& src, uint64_t srcOffset,
            uint64_t numBytes) {
            CopyBufferRegion(Resolve(dst), dstOffset, Resolve(src), srcOffset, numBytes);
        }

        void CopyBufferRegion(Resource* dst, uint64_t dstOffset,
            Resource* src, uint64_t srcOffset,
            uint64_t numBytes) {
            CopyBufferRegion(Resolve(dst), dstOffset, Resolve(src), srcOffset, numBytes);
        }

        void ClearRTV(ResourceIdentifier const& target, float r, float g, float b, float a, RangeSpec range = {}) {
            ClearRTV(Resolve(target), r, g, b, a, range);
        }
        void ClearRTV(Resource* target, float r, float g, float b, float a, RangeSpec range = {}) {
            ClearRTV(Resolve(target), r, g, b, a, range);
        }

        void ClearDSV(ResourceIdentifier const& target, bool clearDepth, float depth, bool clearStencil, uint8_t stencil, RangeSpec range = {}) {
            ClearDSV(Resolve(target), clearDepth, depth, clearStencil, stencil, range);
        }
        void ClearDSV(Resource* target, bool clearDepth, float depth, bool clearStencil, uint8_t stencil, RangeSpec range = {}) {
            ClearDSV(Resolve(target), clearDepth, depth, clearStencil, stencil, range);
        }

        void ClearUavFloat(ResourceIdentifier const& target, float x, float y, float z, float w, RangeSpec range = {}) {
            ClearUavFloat(Resolve(target), x, y, z, w, range);
        }
        void ClearUavFloat(Resource* target, float x, float y, float z, float w, RangeSpec range = {}) {
            ClearUavFloat(Resolve(target), x, y, z, w, range);
        }

        // Produce per-frame data (bytecode + requirements).
        // Call after the pass finishes recording.
        FrameData Finalize() const {
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

    private:
        struct Resolved {
            std::shared_ptr<Resource> keepAlive;
            Resource* raw = nullptr;
            uint64_t globalId = 0;
        };

        Resolved Resolve(ResourceIdentifier const& id) {
            if (!m_resolveFn) {
                throw std::runtime_error("ImmediateCommandList has no ResolveByIdFn");
            }
            auto sp = m_resolveFn(m_resolveUser, id, /*allowFailure=*/false);
            if (!sp) {
                throw std::runtime_error("ImmediateCommandList failed to resolve id: " + id.ToString());
            }
            return Resolve(sp.get(), std::move(sp));
        }

        Resolved Resolve(Resource* p) {
            if (!p) throw std::runtime_error("ImmediateCommandList: null Resource*");
            // TODO: fix this shared_ptr nonsense
			std::shared_ptr<Resource> sp = p->shared_from_this();
            return Resolve(p, std::move(sp));
        }

        Resolved Resolve(Resource* p, std::shared_ptr<Resource> keepAlive) {
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

        ResourceState MakeState(rhi::ResourceAccessType access) const {
            // Match what PassBuilders do (render vs compute sync selection).
            return ResourceState{
                access,
                AccessToLayout(access, /*isRender=*/m_isRenderPass),
                m_isRenderPass ? RenderSyncFromAccess(access) : ComputeSyncFromAccess(access)
            };
        }

        void Track(Resource* pRes, uint64_t rid, RangeSpec range, rhi::ResourceAccessType access) {
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

        void CopyBufferRegion(Resolved const& dst, uint64_t dstOffset,
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

        void ClearRTV(Resolved const& target, float r, float g, float b, float a, RangeSpec range)
        {
            if (!m_dispatch.GetRTV)
                throw std::runtime_error("ImmediateDispatch::GetRTV not set");

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

            if (any)
                Track(target.raw, target.globalId, range, rhi::ResourceAccessType::RenderTarget);
        }


        void ClearDSV(Resolved const& target,
            bool clearDepth, float depth,
            bool clearStencil, uint8_t stencil,
            RangeSpec range)
        {
            if (!clearDepth && !clearStencil)
                return;

            if (!m_dispatch.GetDSV)
                throw std::runtime_error("ImmediateDispatch::GetDSV not set");

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

            if (any)
                Track(target.raw, target.globalId, range, rhi::ResourceAccessType::DepthReadWrite);
        }


        void ClearUavFloat(Resolved const& target, float x, float y, float z, float w, RangeSpec range)
        {
            if (!m_dispatch.GetUavClearInfo)
                throw std::runtime_error("ImmediateDispatch::GetUavClearInfo not set");

            rhi::UavClearFloat value{};
            value.v[0] = x; value.v[1] = y; value.v[2] = z; value.v[3] = w;

            const bool any = ForEachMipSlice(*target.raw, range,
                [&](uint32_t /*mip*/, uint32_t /*slice*/, RangeSpec exact)
                {
                    ClearUavFloatCmd cmd{};
                    cmd.value = value;

                    if (!m_dispatch.GetUavClearInfo(*target.raw, exact, cmd.info))
                        throw std::runtime_error("Immediate clear: GetUavClearInfo failed");

                    if (!cmd.info.shaderVisible.heap.valid() || !cmd.info.cpuVisible.heap.valid())
                        throw std::runtime_error("Immediate clear: invalid UAV descriptor slots");

                    m_writer.WriteOp(Op::ClearUavFloat);
                    m_writer.WritePOD(cmd);
                });

            if (any)
                Track(target.raw, target.globalId, range, rhi::ResourceAccessType::UnorderedAccess);
        }


        static RangeSpec MakeExactMipSlice(uint32_t mip, uint32_t slice) noexcept
        {
            RangeSpec r{};
            r.mipLower = { BoundType::Exact, mip };
            r.mipUpper = { BoundType::Exact, mip };
            r.sliceLower = { BoundType::Exact, slice };
            r.sliceUpper = { BoundType::Exact, slice };
            return r;
        }

        template<class F>
        static bool ForEachMipSlice(Resource& res, RangeSpec range, F&& fn)
        {
            const uint32_t totalMips = res.GetMipLevels();
            const uint32_t totalSlices = res.GetArraySize();

            SubresourceRange sr = ResolveRangeSpec(range, totalMips, totalSlices);
            if (sr.isEmpty())
                return false;

            for (uint32_t s = 0; s < sr.sliceCount; ++s)
            {
                const uint32_t slice = sr.firstSlice + s;
                for (uint32_t m = 0; m < sr.mipCount; ++m)
                {
                    const uint32_t mip = sr.firstMip + m;
                    fn(mip, slice, MakeExactMipSlice(mip, slice));
                }
            }
            return true;
        }

        static void RequireValidSlot(const rhi::DescriptorSlot& s, const char* what)
        {
            // DescriptorSlot::heap is a Handle<>
            if (!s.heap.valid())
                throw std::runtime_error(std::string("Immediate clear: invalid ") + what + " descriptor slot");
        }


    private:
        bool m_isRenderPass = true;

        ImmediateDispatch const& m_dispatch;
        ResolveByIdFn m_resolveFn = nullptr;
        void* m_resolveUser = nullptr;

        BytecodeWriter m_writer;

        // GlobalID -> ptr (so requirements contain shared_ptr<Resource>)
		// TODO: shared_ptr is not good here. Fix requirement ownership model.
        std::unordered_map<uint64_t, std::shared_ptr<Resource>> m_ptrs;

        // GlobalID -> tracker of desired access for this pass's immediate section
        std::unordered_map<uint64_t, SymbolicTracker> m_trackers;
    };

} // namespace rendergraph::imm
