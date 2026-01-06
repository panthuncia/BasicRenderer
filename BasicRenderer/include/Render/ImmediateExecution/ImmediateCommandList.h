#pragma once

#include <cstddef>
#include <vector>
#include <unordered_map>
#include <memory>
#include <type_traits>
#include <stdexcept>

#include <rhi.h>

#include "Resources/ResourceIdentifier.h"
#include "Resources/ResourceStateTracker.h"   // RangeSpec, SymbolicTracker, ResourceState
#include "Render/ResourceRequirements.h"      // ResourceRequirement, ResourceAndRange
#include "Resources/Resource.h"
#include "Resources/GloballyIndexedResource.h"

class RenderGraph;

namespace rg::imm {

    // RenderGraph provides these thunks so the immediate list can resolve identifiers
    // without going through the pass's restricted registry view.
    using ResolveByIdFn = ResourceRegistry::RegistryHandle(*)(void* user, ResourceIdentifier const& id, bool allowFailure);
	using ResolveByPtrFn = ResourceRegistry::RegistryHandle(*)(void* user, Resource* res, bool allowFailure);
    
	// "Dispatch" that lives on RenderGraph so immediate recording can
    // turn a ResourceHandle into low-level RHI handles/descriptor slots at record time.
    // Replay then needs only the RHI command list + bytecode stream.

    struct ImmediateDispatch {
        RenderGraph* user = nullptr;
        rhi::ResourceHandle(*GetResourceHandle)(RenderGraph* user, ResourceRegistry::RegistryHandle r) noexcept = nullptr;

        // These expect RangeSpec that resolves to (at least) one mip/slice.
        rhi::DescriptorSlot(*GetRTV)(RenderGraph* user, ResourceRegistry::RegistryHandle r, RangeSpec range) noexcept = nullptr;
        rhi::DescriptorSlot(*GetDSV)(RenderGraph* user, ResourceRegistry::RegistryHandle r, RangeSpec range) noexcept = nullptr;

        // Returns false if the resource can't provide the required UAV clear info.
        bool (*GetUavClearInfo)(RenderGraph* user, ResourceRegistry::RegistryHandle r, RangeSpec range, rhi::UavClearInfo& out) noexcept = nullptr;
    };

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
            const size_t old = data.size();
            data.resize(old + sizeof(T));
            std::memcpy(data.data() + old, &v, sizeof(T));
        }

    private:
        void Align(const size_t a) {
            const size_t cur = data.size();
            if (const size_t pad = (a == 0) ? 0 : ((a - (cur % a)) % a); pad) {
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

    struct LifetimePin {
        // type-erased owning payload
        std::shared_ptr<void> shared;
        std::unique_ptr<void, void(*)(void*)> unique;
    };

    struct KeepAliveBag {
        std::vector<LifetimePin> pins;
        template<class T>
        uint32_t pinUnique(std::unique_ptr<T> v) {
            pins.push_back(LifetimePin{
                {},
                std::unique_ptr<void, void(*)(void*)>(v.release(), [](void* p) { delete static_cast<T*>(p); })
                });
            return static_cast<uint32_t>(pins.size() - 1);
        }
        template<class T>
        uint32_t pinShared(std::shared_ptr<T> v) {
            pins.push_back(LifetimePin{
                std::static_pointer_cast<void>(std::move(v)),
                std::unique_ptr<void, void(*)(void*)>{ nullptr, +[](void*) noexcept {} } // Annoying
                });
            return static_cast<uint32_t>(pins.size() - 1);
        }
    };

    struct FrameData {
        FrameData() : keepAlive(std::make_unique<KeepAliveBag>()) {}
        std::vector<std::byte> bytecode;                 // replay payload
        std::vector<ResourceRequirement> requirements;   // merged segments
		std::unique_ptr<KeepAliveBag> keepAlive; // Keeps owned resource wrappers alive for the frame. Only used by UploadManager, currently
        void Reset() { bytecode.clear(); requirements.clear(); }
    };

    // Immediate command list: records bytecode + tracks resource access requirements.
    class ImmediateCommandList {
    public:
        ImmediateCommandList(bool isRenderPass,
            ImmediateDispatch const& dispatch,
            ResolveByIdFn resolveByIdFn,
			ResolveByPtrFn resolveByPtrFn,
            void* resolveUser)
            : m_isRenderPass(isRenderPass)
            , m_dispatch(dispatch)
            , m_resolveByIdFn(resolveByIdFn)
            , m_resolveByPtrFn(resolveByPtrFn)
            , m_resolveUser(resolveUser)
			, m_keepAlive(std::make_unique<KeepAliveBag>())
        {
        }

        void Reset();

        // API: resources can be ResourceIdentifier or Resource*

        void CopyBufferRegion(ResourceIdentifier const& dst, const uint64_t dstOffset,
            ResourceIdentifier const& src, const uint64_t srcOffset,
            const uint64_t numBytes) {
            CopyBufferRegion(Resolve(dst), dstOffset, Resolve(src), srcOffset, numBytes);
        }

        void CopyBufferRegion(Resource* dst, const uint64_t dstOffset,
            Resource* src, const uint64_t srcOffset,
            const uint64_t numBytes) {
            CopyBufferRegion(Resolve(dst), dstOffset, Resolve(src), srcOffset, numBytes);
        }

        // For copying from ephemeral resources that the caller is discarding ownership of
		// TODO: Consider making a separate API for "do something and discard" semantics?
		// TODO: If not, add other overloads for ephemeral resources
        void CopyBufferRegion(Resource* dst, const uint64_t dstOffset,
            const std::shared_ptr<Resource>& srcOwned, const uint64_t srcOffset,
			const uint64_t numBytes) {
            const auto dstHandle = Resolve(dst);
			const auto srcHandle = Resolve(srcOwned.get(), srcOwned); // Pin the ephemeral resource
            CopyBufferRegion(dstHandle, dstOffset, srcHandle, srcOffset, numBytes);
		}

		// For copying to ephemeral resources that the caller may discard
        void CopyBufferRegion(const std::shared_ptr<Resource>& dstOwned, const uint64_t dstOffset,
            Resource* src, const uint64_t srcOffset,
            const uint64_t numBytes) {
            const auto dstHandle = Resolve(dstOwned.get(), dstOwned); // Pin the ephemeral resource
            const auto srcHandle = Resolve(src);
            CopyBufferRegion(dstHandle, dstOffset, srcHandle, srcOffset, numBytes);
		}

        void ClearRTV(ResourceIdentifier const& target, const float r, const float g, const float b, const float a, const RangeSpec& range = {}) {
            ClearRTV(Resolve(target), r, g, b, a, range);
        }
        void ClearRTV(Resource* target, const float r, const float g, const float b, const float a, const RangeSpec& range = {}) {
            ClearRTV(Resolve(target), r, g, b, a, range);
        }

        void ClearDSV(ResourceIdentifier const& target, const bool clearDepth, const float depth, const bool clearStencil, const uint8_t stencil, const RangeSpec& range = {}) {
            ClearDSV(Resolve(target), clearDepth, depth, clearStencil, stencil, range);
        }
        void ClearDSV(Resource* target, const bool clearDepth, const float depth, const bool clearStencil, const uint8_t stencil, const RangeSpec& range = {}) {
            ClearDSV(Resolve(target), clearDepth, depth, clearStencil, stencil, range);
        }

        void ClearUavFloat(ResourceIdentifier const& target, const float x, const float y, const float z, const float w, const RangeSpec& range = {}) {
            ClearUavFloat(Resolve(target), x, y, z, w, range);
        }
        void ClearUavFloat(Resource* target, const float x, const float y, const float z, const float w, const RangeSpec& range = {}) {
            ClearUavFloat(Resolve(target), x, y, z, w, range);
        }

        // ---- UAV uint clear ----
        void ClearUavUint(ResourceIdentifier const& target, const uint32_t x, const uint32_t y, const uint32_t z, const uint32_t w, const RangeSpec& range = {}) {
            ClearUavUint(Resolve(target), x, y, z, w, range);
        }
        void ClearUavUint(Resource* target, const uint32_t x, const uint32_t y, const uint32_t z, const uint32_t w, const RangeSpec& range = {}) {
            ClearUavUint(Resolve(target), x, y, z, w, range);
        }

        // ---- Texture region copy (texture -> texture) ----
        void CopyTextureRegion(
            ResourceIdentifier const& dstTex, const uint32_t dstMip, const uint32_t dstSlice, const uint32_t dstX, const uint32_t dstY, const uint32_t dstZ,
            ResourceIdentifier const& srcTex, const uint32_t srcMip, const uint32_t srcSlice, const uint32_t srcX, const uint32_t srcY, const uint32_t srcZ,
            const uint32_t width, const uint32_t height, const uint32_t depth = 1)
        {
            CopyTextureRegion(Resolve(dstTex), dstMip, dstSlice, dstX, dstY, dstZ,
                Resolve(srcTex), srcMip, srcSlice, srcX, srcY, srcZ,
                width, height, depth);
        }

        void CopyTextureRegion(
            Resource* dstTex, const uint32_t dstMip, const uint32_t dstSlice, const uint32_t dstX, const uint32_t dstY, const uint32_t dstZ,
            Resource* srcTex, const uint32_t srcMip, const uint32_t srcSlice, const uint32_t srcX, const uint32_t srcY, const uint32_t srcZ,
            const uint32_t width, const uint32_t height, const uint32_t depth = 1)
        {
            CopyTextureRegion(Resolve(dstTex), dstMip, dstSlice, dstX, dstY, dstZ,
                Resolve(srcTex), srcMip, srcSlice, srcX, srcY, srcZ,
                width, height, depth);
        }

        // ---- Texture <-> buffer via footprint ----
        void CopyTextureToBuffer(ResourceIdentifier const& texture, const uint32_t mip, const uint32_t slice,
            ResourceIdentifier const& buffer,
            rhi::CopyableFootprint const& footprint,
            const uint32_t x = 0, const uint32_t y = 0, const uint32_t z = 0)
        {
            CopyTextureToBuffer(Resolve(texture), mip, slice, Resolve(buffer), footprint, x, y, z);
        }

        void CopyTextureToBuffer(Resource* texture, const uint32_t mip, const uint32_t slice,
            Resource* buffer,
            rhi::CopyableFootprint const& footprint,
            const uint32_t x = 0, const uint32_t y = 0, const uint32_t z = 0)
        {
            CopyTextureToBuffer(Resolve(texture), mip, slice, Resolve(buffer), footprint, x, y, z);
        }

        void CopyBufferToTexture(ResourceIdentifier const& buffer,
            ResourceIdentifier const& texture, const uint32_t mip, const uint32_t slice,
            rhi::CopyableFootprint const& footprint,
            const uint32_t x = 0, const uint32_t y = 0, const uint32_t z = 0)
        {
            CopyBufferToTexture(Resolve(buffer), Resolve(texture), mip, slice, footprint, x, y, z);
        }

        void CopyBufferToTexture(Resource* buffer,
            Resource* texture, const uint32_t mip, const uint32_t slice,
            rhi::CopyableFootprint const& footprint,
            const uint32_t x = 0, const uint32_t y = 0, const uint32_t z = 0)
        {
            CopyBufferToTexture(Resolve(buffer), Resolve(texture), mip, slice, footprint, x, y, z);
        }

		// texture owning override of CopyBufferToTexture
        void CopyBufferToTexture(Resource* buffer,
            const std::shared_ptr<Resource>& texture, const uint32_t mip, const uint32_t slice,
            rhi::CopyableFootprint const& footprint,
            const uint32_t x = 0, const uint32_t y = 0, const uint32_t z = 0)
        {
            const auto bufferHandle = Resolve(buffer);
            const auto textureHandle = Resolve(texture.get(), texture); // Pin the ephemeral resource
            CopyBufferToTexture(bufferHandle, textureHandle, mip, slice, footprint, x, y, z);
        }



        // Produce per-frame data (bytecode + requirements).
        // Call after the pass finishes recording.
        FrameData Finalize();

    private:
        struct Resolved {
            ResourceRegistry::RegistryHandle handle;
        };

        Resolved Resolve(ResourceIdentifier const& id);

        Resolved Resolve(Resource* p);

        Resolved Resolve(Resource* p, const std::shared_ptr<Resource>& keepAlive);

        ResourceState MakeState(rhi::ResourceAccessType access) const;

        void Track(ResourceRegistry::RegistryHandle handle, uint64_t rid, const RangeSpec& range, rhi::ResourceAccessType access);

        void CopyBufferRegion(Resolved const& dst, uint64_t dstOffset,
            Resolved const& src, uint64_t srcOffset,
            uint64_t numBytes);

        void ClearRTV(Resolved const& target, float r, float g, float b, float a, const RangeSpec& range);

        void ClearDSV(Resolved const& target,
            bool clearDepth, float depth,
            bool clearStencil, uint8_t stencil,
            const RangeSpec& range);


        void ClearUavFloat(Resolved const& target, float x, float y, float z, float w, const RangeSpec& range);

        void ClearUavUint(Resolved const& target, uint32_t x, uint32_t y, uint32_t z, uint32_t w, const RangeSpec& range);

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


        static RangeSpec MakeExactMipSlice(const uint32_t mip, const uint32_t slice) noexcept
        {
            RangeSpec r{};
            r.mipLower = { BoundType::Exact, mip };
            r.mipUpper = { BoundType::Exact, mip };
            r.sliceLower = { BoundType::Exact, slice };
            r.sliceUpper = { BoundType::Exact, slice };
            return r;
        }

        template<class F>
        static bool ForEachMipSlice(const ResourceRegistry::RegistryHandle& res, const RangeSpec& range, F&& fn)
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

		// Keep-alive for ephemeral resources only valid during this command list's execution
        // For example, copy for resource resize- the old one is discarded.
		std::unique_ptr<KeepAliveBag> m_keepAlive;
    };

} // namespace rendergraph::imm
