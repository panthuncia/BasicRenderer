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

    // RenderGraph provides these thunks so the immediate list can resolve identifiers
    // without going through the pass's restricted registry view.
    using ResolveByIdFn = ResourceRegistry::RegistryHandle(*)(void* user, ResourceIdentifier const& id, bool allowFailure);
	using ResolveByPtrFn = ResourceRegistry::RegistryHandle(*)(void* user, Resource* res, bool allowFailure);
    
	// "Dispatch" that lives on RenderGraph so immediate recording can
    // turn a ResourceHandle into low-level RHI handles/descriptor slots at record time.
    // Replay then needs only the RHI command list + bytecode stream.

    struct ImmediateDispatch {
        rhi::ResourceHandle(*GetResourceHandle)(ResourceRegistry::RegistryHandle r) noexcept = nullptr;

        // These expect RangeSpec that resolves to (at least) one mip/slice.
        rhi::DescriptorSlot(*GetRTV)(ResourceRegistry::RegistryHandle r, RangeSpec range) noexcept = nullptr;
        rhi::DescriptorSlot(*GetDSV)(ResourceRegistry::RegistryHandle r, RangeSpec range) noexcept = nullptr;

        // Returns false if the resource can't provide the required UAV clear info.
        bool (*GetUavClearInfo)(ResourceRegistry::RegistryHandle r, RangeSpec range, rhi::UavClearInfo& out) noexcept = nullptr;
    };

    inline bool ResolveFirstMipSlice(ResourceRegistry::RegistryHandle r, RangeSpec range, uint32_t& outMip, uint32_t& outSlice) noexcept
    {
        const uint32_t totalMips = r.GetNumMipLevels();
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

        d.GetResourceHandle = +[](ResourceRegistry::RegistryHandle r) noexcept -> rhi::ResourceHandle {
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
        ClearUavUint = 5,
        CopyTextureRegion = 6,
        CopyTextureToBuffer = 7,
        CopyBufferToTexture = 8,
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

    struct ClearUavUintCmd {
        rhi::UavClearInfo info{};
        rhi::UavClearUint value{};
    };

    struct CopyTextureRegionCmd {
        rhi::TextureCopyRegion dst{};
        rhi::TextureCopyRegion src{};
    };

    struct CopyTextureToBufferCmd {
        rhi::BufferTextureCopyFootprint region{};
    };

    struct CopyBufferToTextureCmd {
        rhi::BufferTextureCopyFootprint region{};
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

        bool Empty() const noexcept;
        Op ReadOp();

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
        void Require(size_t n) const;
        void Align(size_t a);

        std::byte const* base = nullptr;
        std::byte const* cur = nullptr;
        std::byte const* end = nullptr;
    };

    // Replay bytecode into a concrete RHI command list.
    void Replay(std::vector<std::byte> const& bytecode, rhi::CommandList& cl);

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
            ResolveByIdFn resolveByIdFn,
            void* resolveUser)
            : m_isRenderPass(isRenderPass)
            , m_dispatch(dispatch)
            , m_resolveByIdFn(resolveByIdFn)
            , m_resolveUser(resolveUser)
        {
        }

        void Reset();

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

        // ---- UAV uint clear ----
        void ClearUavUint(ResourceIdentifier const& target, uint32_t x, uint32_t y, uint32_t z, uint32_t w, RangeSpec range = {}) {
            ClearUavUint(Resolve(target), x, y, z, w, range);
        }
        void ClearUavUint(Resource* target, uint32_t x, uint32_t y, uint32_t z, uint32_t w, RangeSpec range = {}) {
            ClearUavUint(Resolve(target), x, y, z, w, range);
        }

        // ---- Texture region copy (texture -> texture) ----
        void CopyTextureRegion(
            ResourceIdentifier const& dstTex, uint32_t dstMip, uint32_t dstSlice, uint32_t dstX, uint32_t dstY, uint32_t dstZ,
            ResourceIdentifier const& srcTex, uint32_t srcMip, uint32_t srcSlice, uint32_t srcX, uint32_t srcY, uint32_t srcZ,
            uint32_t width, uint32_t height, uint32_t depth = 1)
        {
            CopyTextureRegion(Resolve(dstTex), dstMip, dstSlice, dstX, dstY, dstZ,
                Resolve(srcTex), srcMip, srcSlice, srcX, srcY, srcZ,
                width, height, depth);
        }

        void CopyTextureRegion(
            Resource* dstTex, uint32_t dstMip, uint32_t dstSlice, uint32_t dstX, uint32_t dstY, uint32_t dstZ,
            Resource* srcTex, uint32_t srcMip, uint32_t srcSlice, uint32_t srcX, uint32_t srcY, uint32_t srcZ,
            uint32_t width, uint32_t height, uint32_t depth = 1)
        {
            CopyTextureRegion(Resolve(dstTex), dstMip, dstSlice, dstX, dstY, dstZ,
                Resolve(srcTex), srcMip, srcSlice, srcX, srcY, srcZ,
                width, height, depth);
        }

        // ---- Texture <-> buffer via footprint ----
        void CopyTextureToBuffer(ResourceIdentifier const& texture, uint32_t mip, uint32_t slice,
            ResourceIdentifier const& buffer,
            rhi::CopyableFootprint const& footprint,
            uint32_t x = 0, uint32_t y = 0, uint32_t z = 0)
        {
            CopyTextureToBuffer(Resolve(texture), mip, slice, Resolve(buffer), footprint, x, y, z);
        }

        void CopyTextureToBuffer(Resource* texture, uint32_t mip, uint32_t slice,
            Resource* buffer,
            rhi::CopyableFootprint const& footprint,
            uint32_t x = 0, uint32_t y = 0, uint32_t z = 0)
        {
            CopyTextureToBuffer(Resolve(texture), mip, slice, Resolve(buffer), footprint, x, y, z);
        }

        void CopyBufferToTexture(ResourceIdentifier const& buffer,
            ResourceIdentifier const& texture, uint32_t mip, uint32_t slice,
            rhi::CopyableFootprint const& footprint,
            uint32_t x = 0, uint32_t y = 0, uint32_t z = 0)
        {
            CopyBufferToTexture(Resolve(buffer), Resolve(texture), mip, slice, footprint, x, y, z);
        }

        void CopyBufferToTexture(Resource* buffer,
            Resource* texture, uint32_t mip, uint32_t slice,
            rhi::CopyableFootprint const& footprint,
            uint32_t x = 0, uint32_t y = 0, uint32_t z = 0)
        {
            CopyBufferToTexture(Resolve(buffer), Resolve(texture), mip, slice, footprint, x, y, z);
        }


        // Produce per-frame data (bytecode + requirements).
        // Call after the pass finishes recording.
        FrameData Finalize() const;

    private:
        struct Resolved {
            ResourceRegistry::RegistryHandle handle;
        };

        Resolved Resolve(ResourceIdentifier const& id);

        Resolved Resolve(Resource* p);

        Resolved Resolve(Resource* p, std::shared_ptr<Resource> keepAlive);

        ResourceState MakeState(rhi::ResourceAccessType access) const;

        void Track(ResourceRegistry::RegistryHandle handle, uint64_t rid, RangeSpec range, rhi::ResourceAccessType access);

        void CopyBufferRegion(Resolved const& dst, uint64_t dstOffset,
            Resolved const& src, uint64_t srcOffset,
            uint64_t numBytes);

        void ClearRTV(Resolved const& target, float r, float g, float b, float a, RangeSpec range);

        void ClearDSV(Resolved const& target,
            bool clearDepth, float depth,
            bool clearStencil, uint8_t stencil,
            RangeSpec range);


        void ClearUavFloat(Resolved const& target, float x, float y, float z, float w, RangeSpec range);

        void ClearUavUint(Resolved const& target, uint32_t x, uint32_t y, uint32_t z, uint32_t w, RangeSpec range);

        void CopyTextureRegion(
            Resolved const& dst, uint32_t dstMip, uint32_t dstSlice, uint32_t dstX, uint32_t dstY, uint32_t dstZ,
            Resolved const& src, uint32_t srcMip, uint32_t srcSlice, uint32_t srcX, uint32_t srcY, uint32_t srcZ,
            uint32_t width, uint32_t height, uint32_t depth);

        void CopyTextureToBuffer(
            Resolved const& texture, uint32_t mip, uint32_t slice,
            Resolved const& buffer,
            rhi::CopyableFootprint const& footprint,
            uint32_t x, uint32_t y, uint32_t z);

        void CopyBufferToTexture(
            Resolved const& buffer,
            Resolved const& texture, uint32_t mip, uint32_t slice,
            rhi::CopyableFootprint const& footprint,
            uint32_t x, uint32_t y, uint32_t z);


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
        static bool ForEachMipSlice(const ResourceRegistry::RegistryHandle& res, RangeSpec range, F&& fn)
        {
            const uint32_t totalMips = res.GetNumMipLevels();
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
        ResolveByIdFn m_resolveByIdFn = nullptr;
		ResolveByPtrFn m_resolveByPtrFn = nullptr;

        void* m_resolveUser = nullptr;

        BytecodeWriter m_writer;

        // GlobalID -> handle (for ResourceRequirements)
        std::unordered_map<uint64_t, ResourceRegistry::RegistryHandle> m_handles;

        // GlobalID -> tracker of desired access for this pass's immediate section
        std::unordered_map<uint64_t, SymbolicTracker> m_trackers;
    };

} // namespace rendergraph::imm
