#pragma once
#include "rhi.h"

// RHIHelpers.h
#pragma once
#include <utility>     // std::move
#include <type_traits> // std::is_trivially_copyable_v
#include <dxgi1_6.h>
#include <string>
#include <stdexcept>

namespace rhi {
    namespace helpers {

		// AnyObjectPtr - type-erased, move-only holder for ObjectPtr<T>.
		//
		// Stores any of the ObjectPtr<> specializations inline
		// so they can live together in a single container (e.g. a deletion queue).
        namespace detail {
            template<size_t A, size_t B>
            struct static_max2 { static constexpr size_t value = (A > B) ? A : B; };

            template<size_t A, size_t... Rest>
            struct static_max : static_max2<A, static_max<Rest...>::value> {};
            template<size_t A>
            struct static_max<A> { static constexpr size_t value = A; };

            template<class... Ts>
            struct max_sizeof : static_max<sizeof(Ts)...> {};
            template<class... Ts>
            struct max_alignof : static_max<alignof(Ts)...> {};
        } // namespace detail

        inline constexpr size_t RHI_ANYOBJECTPTR_INLINE_SIZE = detail::max_sizeof<
            CommandAllocatorPtr,
            CommandListPtr,
            SwapchainPtr,
            ResourcePtr,
            QueryPoolPtr,
            PipelinePtr,
            PipelineLayoutPtr,
            CommandSignaturePtr,
            DescriptorHeapPtr,
            TimelinePtr,
            HeapPtr,
            DevicePtr
        >::value;

        inline constexpr size_t RHI_ANYOBJECTPTR_INLINE_ALIGN = detail::max_alignof<
            CommandAllocatorPtr,
            CommandListPtr,
            SwapchainPtr,
            ResourcePtr,
            QueryPoolPtr,
            PipelinePtr,
            PipelineLayoutPtr,
            CommandSignaturePtr,
            DescriptorHeapPtr,
            TimelinePtr,
            HeapPtr,
            DevicePtr
        >::value;

        class AnyObjectPtr {
        public:
            AnyObjectPtr() = default;
            ~AnyObjectPtr() { Reset(); }

            AnyObjectPtr(const AnyObjectPtr&) = delete;
            AnyObjectPtr& operator=(const AnyObjectPtr&) = delete;

            AnyObjectPtr(AnyObjectPtr&& o) noexcept { MoveFrom(o); }
            AnyObjectPtr& operator=(AnyObjectPtr&& o) noexcept {
                if (this != &o) {
                    Reset();
                    MoveFrom(o);
                }
                return *this;
            }

            // Construct from any ObjectPtr<T> rvalue.
            template<class TObject>
            AnyObjectPtr(ObjectPtr<TObject>&& p) noexcept {
                Emplace<ObjectPtr<TObject>>(std::move(p));
            }

            AnyObjectPtr(DevicePtr&& p) noexcept { Emplace<DevicePtr>(std::move(p)); }

            // In-place construct a specific ObjectPtr type.
            template<class T, class... Args>
            T& Emplace(Args&&... args) noexcept {
                static_assert(std::is_nothrow_move_constructible_v<T>,
                    "AnyObjectPtr requires nothrow-move types.");
                static_assert(sizeof(T) <= RHI_ANYOBJECTPTR_INLINE_SIZE,
                    "Type too large for AnyObjectPtr inline storage. Increase RHI_ANYOBJECTPTR_INLINE_SIZE.");
                static_assert(alignof(T) <= RHI_ANYOBJECTPTR_INLINE_ALIGN,
                    "Type alignment too strict for AnyObjectPtr inline storage. Increase RHI_ANYOBJECTPTR_INLINE_ALIGN.");

                Reset();
                vt_ = VTableFor<T>();
                return *::new (static_cast<void*>(storage_)) T(std::forward<Args>(args)...);
            }

            void Reset() noexcept {
                if (vt_) {
                    vt_->destroy(storage_);
                    vt_ = nullptr;
                }
            }

            explicit operator bool() const noexcept { return vt_ != nullptr; }
            bool HasValue() const noexcept { return vt_ != nullptr; }

            // Query / access (optional convenience)
            template<class T>
            bool Is() const noexcept {
                return vt_ == VTableFor<T>();
            }

            template<class T>
            T* GetIf() noexcept {
                return Is<T>() ? reinterpret_cast<T*>(storage_) : nullptr;
            }

            template<class T>
            const T* GetIf() const noexcept {
                return Is<T>() ? reinterpret_cast<const T*>(storage_) : nullptr;
            }

        private:
            struct VTable {
                void (*destroy)(void*) noexcept;
                void (*move)(void* dst, void* src) noexcept;
            };

            template<class T>
            static const VTable* VTableFor() noexcept {
                static const VTable v{
                    // destroy
                    [](void* p) noexcept {
                        static_cast<T*>(p)->~T();
                    },
                    // move-construct into dst, then destroy src
                    [](void* dst, void* src) noexcept {
                        new (dst) T(std::move(*static_cast<T*>(src)));
                        static_cast<T*>(src)->~T();
                    }
                };
                return &v;
            }

            void MoveFrom(AnyObjectPtr& o) noexcept {
                vt_ = o.vt_;
                if (vt_) {
                    vt_->move(storage_, o.storage_);
                    o.vt_ = nullptr;
                }
            }

        private:
            alignas(RHI_ANYOBJECTPTR_INLINE_ALIGN) std::byte storage_[RHI_ANYOBJECTPTR_INLINE_SIZE]{};
            const VTable* vt_ = nullptr;
        };

        static inline Format ToRHI(const DXGI_FORMAT f) {
            switch (f) {
            case DXGI_FORMAT_UNKNOWN: return Format::Unknown;
            case DXGI_FORMAT_R32G32B32A32_TYPELESS: return Format::R32G32B32A32_Typeless;
            case DXGI_FORMAT_R32G32B32A32_FLOAT: return Format::R32G32B32A32_Float;
            case DXGI_FORMAT_R32G32B32A32_UINT: return Format::R32G32B32A32_UInt;
            case DXGI_FORMAT_R32G32B32A32_SINT: return Format::R32G32B32A32_SInt;
            case DXGI_FORMAT_R32G32B32_TYPELESS: return Format::R32G32B32_Typeless;
            case DXGI_FORMAT_R32G32B32_FLOAT: return Format::R32G32B32_Float;
            case DXGI_FORMAT_R32G32B32_UINT: return Format::R32G32B32_UInt;
            case DXGI_FORMAT_R32G32B32_SINT: return Format::R32G32B32_SInt;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS: return Format::R16G16B16A16_Typeless;
            case DXGI_FORMAT_R16G16B16A16_FLOAT: return Format::R16G16B16A16_Float;
            case DXGI_FORMAT_R16G16B16A16_UNORM: return Format::R16G16B16A16_UNorm;
            case DXGI_FORMAT_R16G16B16A16_UINT: return Format::R16G16B16A16_UInt;
            case DXGI_FORMAT_R16G16B16A16_SNORM: return Format::R16G16B16A16_SNorm;
            case DXGI_FORMAT_R16G16B16A16_SINT: return Format::R16G16B16A16_SInt;
            case DXGI_FORMAT_R32G32_TYPELESS: return Format::R32G32_Typeless;
            case DXGI_FORMAT_R32G32_FLOAT: return Format::R32G32_Float;
            case DXGI_FORMAT_R32G32_UINT: return Format::R32G32_UInt;
            case DXGI_FORMAT_R32G32_SINT: return Format::R32G32_SInt;
            case DXGI_FORMAT_R10G10B10A2_TYPELESS: return Format::R10G10B10A2_Typeless;
            case DXGI_FORMAT_R10G10B10A2_UNORM: return Format::R10G10B10A2_UNorm;
            case DXGI_FORMAT_R10G10B10A2_UINT: return Format::R10G10B10A2_UInt;
            case DXGI_FORMAT_R11G11B10_FLOAT: return Format::R11G11B10_Float;
            case DXGI_FORMAT_R8G8B8A8_TYPELESS: return Format::R8G8B8A8_Typeless;
            case DXGI_FORMAT_R8G8B8A8_UNORM: return Format::R8G8B8A8_UNorm;
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return Format::R8G8B8A8_UNorm_sRGB;
            case DXGI_FORMAT_R8G8B8A8_UINT: return Format::R8G8B8A8_UInt;
            case DXGI_FORMAT_R8G8B8A8_SNORM: return Format::R8G8B8A8_SNorm;
            case DXGI_FORMAT_R8G8B8A8_SINT: return Format::R8G8B8A8_SInt;
            case DXGI_FORMAT_R16G16_TYPELESS: return Format::R16G16_Typeless;
            case DXGI_FORMAT_R16G16_FLOAT: return Format::R16G16_Float;
            case DXGI_FORMAT_R16G16_UNORM: return Format::R16G16_UNorm;
            case DXGI_FORMAT_R16G16_UINT: return Format::R16G16_UInt;
            case DXGI_FORMAT_R16G16_SNORM: return Format::R16G16_SNorm;
            case DXGI_FORMAT_R16G16_SINT: return Format::R16G16_SInt;
            case DXGI_FORMAT_R32_TYPELESS: return Format::R32_Typeless;
            case DXGI_FORMAT_D32_FLOAT: return Format::D32_Float;
            case DXGI_FORMAT_R32_FLOAT: return Format::R32_Float;
            case DXGI_FORMAT_R32_UINT: return Format::R32_UInt;
            case DXGI_FORMAT_R32_SINT: return Format::R32_SInt;
            case DXGI_FORMAT_R8G8_TYPELESS: return Format::R8G8_Typeless;
            case DXGI_FORMAT_R8G8_UNORM: return Format::R8G8_UNorm;
            case DXGI_FORMAT_R8G8_UINT: return Format::R8G8_UInt;
            case DXGI_FORMAT_R8G8_SNORM: return Format::R8G8_SNorm;
            case DXGI_FORMAT_R8G8_SINT: return Format::R8G8_SInt;
            case DXGI_FORMAT_R16_TYPELESS: return Format::R16_Typeless;
            case DXGI_FORMAT_R16_FLOAT: return Format::R16_Float;
            case DXGI_FORMAT_R16_UNORM: return Format::R16_UNorm;
            case DXGI_FORMAT_R16_UINT: return Format::R16_UInt;
            case DXGI_FORMAT_R16_SNORM: return Format::R16_SNorm;
            case DXGI_FORMAT_R16_SINT: return Format::R16_SInt;
            case DXGI_FORMAT_R8_TYPELESS: return Format::R8_Typeless;
            case DXGI_FORMAT_R8_UNORM: return Format::R8_UNorm;
            case DXGI_FORMAT_R8_UINT: return Format::R8_UInt;
            case DXGI_FORMAT_R8_SNORM: return Format::R8_SNorm;
            case DXGI_FORMAT_R8_SINT: return Format::R8_SInt;
            case DXGI_FORMAT_BC1_TYPELESS: return Format::BC1_Typeless;
            case DXGI_FORMAT_BC1_UNORM: return Format::BC1_UNorm;
            case DXGI_FORMAT_BC1_UNORM_SRGB: return Format::BC1_UNorm_sRGB;
            case DXGI_FORMAT_BC2_TYPELESS: return Format::BC2_Typeless;
            case DXGI_FORMAT_BC2_UNORM: return Format::BC2_UNorm;
            case DXGI_FORMAT_BC2_UNORM_SRGB: return Format::BC2_UNorm_sRGB;
            case DXGI_FORMAT_BC3_TYPELESS: return Format::BC3_Typeless;
            case DXGI_FORMAT_BC3_UNORM: return Format::BC3_UNorm;
            case DXGI_FORMAT_BC3_UNORM_SRGB: return Format::BC3_UNorm_sRGB;
            case DXGI_FORMAT_BC4_TYPELESS: return Format::BC4_Typeless;
            case DXGI_FORMAT_BC4_UNORM: return Format::BC4_UNorm;
            case DXGI_FORMAT_BC4_SNORM: return Format::BC4_SNorm;
            case DXGI_FORMAT_BC5_TYPELESS: return Format::BC5_Typeless;
            case DXGI_FORMAT_BC5_UNORM: return Format::BC5_UNorm;
            case DXGI_FORMAT_BC5_SNORM: return Format::BC5_SNorm;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS: return Format::B8G8R8A8_Typeless;
            case DXGI_FORMAT_B8G8R8A8_UNORM: return Format::B8G8R8A8_UNorm;
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return Format::B8G8R8A8_UNorm_sRGB;
            case DXGI_FORMAT_BC6H_TYPELESS: return Format::BC6H_Typeless;
            case DXGI_FORMAT_BC6H_UF16: return Format::BC6H_UF16;
            case DXGI_FORMAT_BC6H_SF16: return Format::BC6H_SF16;
            case DXGI_FORMAT_BC7_TYPELESS: return Format::BC7_Typeless;
            case DXGI_FORMAT_BC7_UNORM: return Format::BC7_UNorm;
            case DXGI_FORMAT_BC7_UNORM_SRGB: return Format::BC7_UNorm_sRGB;
            default: return Format::Unknown;
            }
        }

        inline bool IsTextureResourceType(ResourceType type) noexcept {
            return (type == ResourceType::Texture1D ||
                type == ResourceType::Texture2D ||
                type == ResourceType::Texture3D);
        }
        // --- Helper in the style of CD3DX12_RESOURCE_DESC ---
        struct ResourceDesc : public rhi::ResourceDesc
        {
            // Default
            constexpr ResourceDesc() noexcept : rhi::ResourceDesc{} {
                // Make union have a defined active member; pick buffer by default.
                buffer = BufferDesc{};
            }

            // Factories
            static constexpr ResourceDesc Buffer(uint64_t sizeBytes,
                HeapType memory = HeapType::DeviceLocal,
                ResourceFlags flags = {},
                const char* debugName = nullptr) noexcept
            {
                rhi::helpers::ResourceDesc d{};
                d.type = ResourceType::Buffer;
                d.resourceFlags = flags;
                d.debugName = debugName;
                d.buffer = BufferDesc{ sizeBytes };
                d.heapType = memory;
                return d;
            }

            static constexpr ResourceDesc Texture(
                ResourceType type,
                Format format,
                HeapType memory,
                uint32_t width,
                uint32_t height = 1,
                uint16_t depthOrLayers = 1,
                uint16_t mipLevels = 1,
                uint32_t sampleCount = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* debugName = nullptr) noexcept
            {
                ResourceDesc d{};
                d.type = type;
                d.resourceFlags = flags;
                d.debugName = debugName;
                d.heapType = memory;
                d.texture = TextureDesc{
                    /*format*/        format,
                    /*width*/         width,
                    /*height*/        height,
                    /*depthOrLayers*/ depthOrLayers,
                    /*mipLevels*/     mipLevels,
                    /*sampleCount*/   sampleCount,
                    /*initialLayout*/ initial,
                    /*optimizedClear*/clear
                };
                return d;
            }

            // Shorthands (mirroring CD3DX12 convenience)
            //static constexpr CRHI_RESOURCE_DESC Tex1D(Format fmt, uint32_t w,
            //    uint16_t mips = 1, uint16_t array = 1,
            //    ResourceLayout initial = ResourceLayout::Undefined,
            //    const ClearValue* clear = nullptr,
            //    ResourceFlags flags = {},
            //    const char* name = nullptr) noexcept
            //{
            //    return Texture(array > 1 ? TextureViewDim::Tex1DArray : TextureViewDim::Tex1D,
            //        fmt, w, 1, array, mips, initial, clear, flags, name);
            //}

            static constexpr ResourceDesc Tex2D(Format fmt, HeapType memory, uint32_t w, uint32_t h,
                uint16_t mips = 1, uint32_t sampleCount = 1, uint16_t array = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* name = nullptr) noexcept
            {
                return Texture(ResourceType::Texture2D,
                    fmt, memory, w, h, array, mips, sampleCount, initial, clear, flags, name);
            }

            static constexpr ResourceDesc Tex3D(Format fmt, HeapType memory, uint32_t w, uint32_t h, uint16_t d,
                uint16_t mips = 1, uint32_t sampleCount = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* name = nullptr) noexcept
            {
                return Texture(ResourceType::Texture3D, fmt, memory, w, h, d, mips, sampleCount, initial, clear, flags, name);
            }

            static constexpr ResourceDesc TexCube(Format fmt, HeapType memory, uint32_t edge,
                uint16_t mips = 1, uint32_t sampleCount = 1, uint16_t cubes = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* name = nullptr) noexcept
            {
                const bool isArray = (cubes > 1);
                const uint16_t totalLayers = static_cast<uint16_t>(6 * cubes);
                return Texture(ResourceType::Texture2D,
                    fmt, memory, edge, edge, totalLayers, mips, sampleCount, initial, clear, flags, name);
            }

            // Light builder API: works with lvalues and rvalues
            constexpr ResourceDesc& WithFlags(ResourceFlags f) & noexcept { this->resourceFlags = f; return *this; }
            constexpr ResourceDesc&& WithFlags(ResourceFlags f) && noexcept { this->resourceFlags = f; return std::move(*this); }

            constexpr ResourceDesc& DebugName(const char* n) & noexcept { this->debugName = n; return *this; }
            constexpr ResourceDesc&& DebugName(const char* n) && noexcept { this->debugName = n; return std::move(*this); }

            // Texture-specific tweaks
            const ResourceDesc& InitialLayout(ResourceLayout l) & noexcept
            {
                if (IsTextureResourceType(type)) texture.initialLayout = l;
                return *this;
            }
            const ResourceDesc&& InitialLayout(ResourceLayout l) && noexcept
            {
                if (IsTextureResourceType(type)) texture.initialLayout = l;
                return std::move(*this);
            }
            const ResourceDesc& OptimizedClear(const ClearValue* cv) & noexcept
            {
                if (IsTextureResourceType(type)) texture.optimizedClear = cv;
                return *this;
            }
            const ResourceDesc&& OptimizedClear(const ClearValue* cv) && noexcept
            {
                if (IsTextureResourceType(type)) texture.optimizedClear = cv;
                return std::move(*this);
            }

            // Helpers
            const bool IsBuffer()  const noexcept { return type == ResourceType::Buffer; }
            const bool IsTexture() const noexcept { return IsTextureResourceType(type); }
        };

        static_assert(std::is_trivially_copyable_v<rhi::ResourceDesc>, "Keep ResourceDesc trivially copyable for constexpr friendliness.");

        enum UploadFlags : uint32_t {
            Upload_None = 0,
            Upload_ManageBarriers = 1 << 0, // if set, helper will transition dst to CopyDest and back
        };
        inline UploadFlags operator|(UploadFlags a, UploadFlags b) {
            return static_cast<UploadFlags>(uint32_t(a) | uint32_t(b));
        }

        static inline bool IsBlockCompressed(Format f) {
            switch (f) {
            case Format::BC1_Typeless: case Format::BC1_UNorm: case Format::BC1_UNorm_sRGB:
            case Format::BC2_Typeless: case Format::BC2_UNorm: case Format::BC2_UNorm_sRGB:
            case Format::BC3_Typeless: case Format::BC3_UNorm: case Format::BC3_UNorm_sRGB:
            case Format::BC4_Typeless: case Format::BC4_UNorm: case Format::BC4_SNorm:
            case Format::BC5_Typeless: case Format::BC5_UNorm: case Format::BC5_SNorm:
            case Format::BC6H_Typeless: case Format::BC6H_UF16: case Format::BC6H_SF16:
            case Format::BC7_Typeless: case Format::BC7_UNorm: case Format::BC7_UNorm_sRGB:
                return true;
            default: return false;
            }
        }

        static inline uint32_t BytesPerBlock(Format f) {
            switch (f) {
            case Format::BC1_Typeless: case Format::BC1_UNorm: case Format::BC1_UNorm_sRGB:
            case Format::BC4_Typeless: case Format::BC4_UNorm: case Format::BC4_SNorm:
                return 8;
            case Format::BC2_Typeless: case Format::BC2_UNorm: case Format::BC2_UNorm_sRGB:
            case Format::BC3_Typeless: case Format::BC3_UNorm: case Format::BC3_UNorm_sRGB:
            case Format::BC5_Typeless: case Format::BC5_UNorm: case Format::BC5_SNorm:
            case Format::BC6H_Typeless: case Format::BC6H_UF16: case Format::BC6H_SF16:
            case Format::BC7_Typeless: case Format::BC7_UNorm: case Format::BC7_UNorm_sRGB:
                return 16;
            default: break;
            }
            // Uncompressed: bytes-per-pixel
            switch (f) {
            case Format::R8_UNorm: case Format::R8_UInt: case Format::R8_SNorm: case Format::R8_SInt:
            case Format::R8_Typeless: return 1;
            case Format::R8G8_UNorm: case Format::R8G8_UInt: case Format::R8G8_SNorm: case Format::R8G8_SInt:
            case Format::R8G8_Typeless: return 2;
            case Format::R8G8B8A8_UNorm: case Format::R8G8B8A8_UNorm_sRGB: case Format::R8G8B8A8_UInt:
            case Format::R8G8B8A8_SNorm: case Format::R8G8B8A8_SInt: case Format::R8G8B8A8_Typeless:
            case Format::B8G8R8A8_Typeless: case Format::B8G8R8A8_UNorm: case Format::B8G8R8A8_UNorm_sRGB:
            case Format::R16_UNorm: case Format::R16_UInt: case Format::R16_SNorm: case Format::R16_SInt:
            case Format::R16_Float: case Format::R16_Typeless:
                return 4; // 32bpp (either RGBA8 or R16)
            case Format::R16G16_UNorm: case Format::R16G16_UInt: case Format::R16G16_SNorm: case Format::R16G16_SInt:
            case Format::R16G16_Float: case Format::R16G16_Typeless:
            case Format::R32_Float: case Format::R32_UInt: case Format::R32_SInt: case Format::R32_Typeless:
                return 8; // 64bpp
            case Format::R10G10B10A2_UNorm: case Format::R10G10B10A2_UInt: case Format::R10G10B10A2_Typeless:
            case Format::R11G11B10_Float:
                return 4; // packed 32bpp
            case Format::R32G32_Float: case Format::R32G32_UInt: case Format::R32G32_SInt: case Format::R32G32_Typeless:
            case Format::R16G16B16A16_UNorm: case Format::R16G16B16A16_UInt: case Format::R16G16B16A16_SNorm:
            case Format::R16G16B16A16_SInt: case Format::R16G16B16A16_Float: case Format::R16G16B16A16_Typeless:
                return 8; // 64bpp (R32G32) or 64bpp per pixel (R16G16B16A16)
            case Format::R32G32B32_Float: case Format::R32G32B32_UInt: case Format::R32G32B32_SInt: case Format::R32G32B32_Typeless:
                return 12; // 96bpp
            case Format::R32G32B32A32_Float: case Format::R32G32B32A32_UInt: case Format::R32G32B32A32_SInt: case Format::R32G32B32A32_Typeless:
                return 16; // 128bpp
            default: return 0; // Unsupported / unknown
            }
        }

        static inline uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + (a - 1)) & ~(a - 1); }
        static inline uint64_t AlignUp64(uint64_t v, uint64_t a) { return (v + (a - 1)) & ~(a - 1); }

        struct SubresourceData {
            const void* pData = nullptr;
            uint32_t    rowPitch = 0;   // bytes in source
            uint32_t    slicePitch = 0; // bytes in source (rowPitch * rows for 2D)
        };


        struct TextureUploadFootprint {
            uint64_t offset = 0;   // bytes from start of upload allocation
            uint32_t rowPitch = 0; // destination row pitch in upload buffer
            uint32_t rowSize = 0;  // bytes to copy per row (no padding)
            uint32_t rows = 0;     // number of rows to copy
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t depth = 1;

            uint32_t mip = 0;
            uint32_t arraySlice = 0;
            uint32_t zSlice = 0;

            uint32_t srcIndex = 0; // index into the provided SubresourceData span
        };

        struct TextureUploadPlan {
            uint64_t totalSize = 0;
            std::vector<TextureUploadFootprint> footprints;
        };

        // Computes the required upload buffer size and a list of placed footprints for all non-null subresources.
        // Intended for 2D, 2D arrays, and cubemaps (depthOrLayers should be 1 in those cases).
        // 3D texture support is intentionally limited (footprints are generated per Z slice, but copies are still 2D-style).
        inline TextureUploadPlan PlanTextureUploadSubresources(
            Format fmt,
            uint32_t baseWidth,
            uint32_t baseHeight,
            uint32_t depthOrLayers,
            uint32_t mipLevels,
            uint32_t arraySize,
            Span<const SubresourceData> srcSubresources)
        {
            TextureUploadPlan plan{};

            const bool bc = IsBlockCompressed(fmt);
            const uint32_t blockW = bc ? 4u : 1u;
            const uint32_t blockH = bc ? 4u : 1u;
            const uint32_t bytesPerBlock = BytesPerBlock(fmt);

            // Safe cross-API alignment (matches D3D12 requirements; Vulkan is OK with these too)
            constexpr uint32_t RowPitchAlign = 256;  // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
            constexpr uint64_t PlacementAlign = 512; // D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT

            const uint32_t depthSlices = (arraySize > 1) ? 1u : std::max(1u, depthOrLayers);

            auto idxOf = [&](uint32_t a, uint32_t m, uint32_t z) -> uint32_t {
                return (a * mipLevels + m) * depthSlices + z;
                };

            uint64_t total = 0;
            plan.footprints.reserve(size_t(arraySize) * size_t(mipLevels) * size_t(depthSlices));

            for (uint32_t a = 0; a < arraySize; ++a) {
                for (uint32_t m = 0; m < mipLevels; ++m) {
                    const uint32_t mipW = std::max(1u, baseWidth >> m);
                    const uint32_t mipH = std::max(1u, baseHeight >> m);

                    const uint32_t bw = (mipW + (blockW - 1)) / blockW; // in blocks
                    const uint32_t bh = (mipH + (blockH - 1)) / blockH; // in blocks

                    const uint32_t rowSize = bc ? (bw * bytesPerBlock) : (mipW * bytesPerBlock);
                    const uint32_t rows = bc ? bh : mipH;

                    const uint32_t rowPitch = AlignUp(rowSize, RowPitchAlign);
                    const uint32_t slicePitch = rowPitch * rows;

                    for (uint32_t z = 0; z < depthSlices; ++z) {
                        const uint32_t srcIndex = idxOf(a, m, z);
                        if (srcIndex >= srcSubresources.size) {
                            continue;
                        }

                        const auto& src = srcSubresources[srcIndex];
                        if (!src.pData) continue;

                        TextureUploadFootprint fp{};
                        fp.offset = AlignUp64(total, PlacementAlign);
                        fp.rowPitch = rowPitch;
                        fp.rowSize = rowSize;
                        fp.rows = rows;
                        fp.width = mipW;
                        fp.height = mipH;
                        fp.depth = 1;
                        fp.mip = m;
                        fp.arraySlice = a;
                        fp.zSlice = z;
                        fp.srcIndex = srcIndex;

                        total = fp.offset + slicePitch;
                        plan.footprints.push_back(fp);
                    }
                }
            }

            plan.totalSize = total;
            return plan;
        }

        // Writes the planned subresources into an already-mapped upload buffer allocation.
        // baseOffset is added to each footprint offset (useful when sub-allocating from a ring/page allocator).
        inline void WriteTextureUploadSubresources(
            const TextureUploadPlan& plan,
            Span<const SubresourceData> srcSubresources,
            void* mappedUpload,
            uint64_t baseOffset = 0)
        {
            if (!mappedUpload) return;
            uint8_t* dstBase = static_cast<uint8_t*>(mappedUpload);

            for (const auto& fp : plan.footprints) {
                if (fp.srcIndex >= srcSubresources.size) continue;
                const auto& src = srcSubresources[fp.srcIndex];
                if (!src.pData) continue;

                const uint8_t* srcPtr = static_cast<const uint8_t*>(src.pData);
                uint8_t* dstPtr = dstBase + baseOffset + fp.offset;

                for (uint32_t r = 0; r < fp.rows; ++r) {
                    const uint8_t* s = srcPtr + size_t(r) * src.rowPitch;
                    uint8_t* d = dstPtr + size_t(r) * fp.rowPitch;
                    std::memcpy(d, s, fp.rowSize);
                }

                // NOTE: 3D uploads would need per-z repetition into subsequent slicePitch regions.
            }
        }

        // Returns an upload buffer and records CopyBufferToTexture calls for all non-null subresources.
        // Assumes destination texture is already in CopyDest state/layout.
        // Does NOT transition states; caller should do that before/after.
        inline ResourcePtr UpdateTextureSubresources(
            Device& dev,
            CommandList& cl,
            Resource& dstTexture,
            Format fmt,
            uint32_t baseWidth,
            uint32_t baseHeight,
            uint32_t depthOrLayers,   // for 3D textures: base depth; for 2D arrays/cubes: array size (layers or faces*layers)
            uint32_t mipLevels,
            uint32_t arraySize,       // number of array slices (for cube use 6*layers)
            Span<const SubresourceData> srcSubresources) // size expected: arraySize * mipLevels * (depth if 3D)
        {
            TextureUploadPlan plan = PlanTextureUploadSubresources(
                fmt, baseWidth, baseHeight, depthOrLayers, mipLevels, arraySize, srcSubresources);

            if (plan.totalSize == 0 || plan.footprints.empty()) {
                return {}; // nothing to upload
            }

            // Create UPLOAD buffer
            ResourceDesc upDesc{};
            upDesc.type = ResourceType::Buffer;
            upDesc.heapType = HeapType::Upload;
            upDesc.resourceFlags = rhi::ResourceFlags::RF_None;
            upDesc.buffer.sizeBytes = plan.totalSize;
            upDesc.debugName = "TextureUpload";
            ResourcePtr upload;
            auto result = dev.CreateCommittedResource(upDesc, upload);
            if (Failed(result)) {
                throw std::runtime_error("Failed to create texture upload buffer.");
                return {};
            }

            // Map once, copy rows for each planned subresource
            void* mapped = nullptr;
            upload->Map(&mapped, 0, plan.totalSize);
            WriteTextureUploadSubresources(plan, srcSubresources, mapped, 0);
            upload->Unmap(0, 0);

            // Record GPU copies: one per subresource (or Z slice)
            for (const auto& fp : plan.footprints) {
                rhi::BufferTextureCopyFootprint f{};
                f.buffer = upload->GetHandle();
                f.texture = dstTexture.GetHandle();
                f.arraySlice = fp.arraySlice;
                f.mip = fp.mip;
                f.x = 0; f.y = 0; f.z = fp.zSlice;

                f.footprint.offset = fp.offset;
                f.footprint.rowPitch = fp.rowPitch;
                f.footprint.width = fp.width;
                f.footprint.height = fp.height;
                f.footprint.depth = fp.depth;

                cl.CopyBufferToTexture(f);
            }

            return upload; // keep alive until GPU finishes (caller can fence/wait)
        }

        struct OwnedBarrierBatch {
            std::vector<TextureBarrier> textures;
            std::vector<BufferBarrier>  buffers;
            std::vector<GlobalBarrier>  globals;

            // Convert owned storage to a non-owning BarrierBatch view
            BarrierBatch View() const noexcept {
                BarrierBatch b{};
                b.textures = { textures.empty() ? nullptr : textures.data(),
                               static_cast<uint32_t>(textures.size()) };
                b.buffers = { buffers.empty() ? nullptr : buffers.data(),
                               static_cast<uint32_t>(buffers.size()) };
                b.globals = { globals.empty() ? nullptr : globals.data(),
                               static_cast<uint32_t>(globals.size()) };
                return b;
            }
            void Append(const BarrierBatch& src) {
                if (src.textures.size) {
                    const auto* p = src.textures.data;
                    textures.insert(textures.end(), p, p + src.textures.size);
                }
                if (src.buffers.size) {
                    const auto* p = src.buffers.data;
                    buffers.insert(buffers.end(), p, p + src.buffers.size);
                }
                if (src.globals.size) {
                    const auto* p = src.globals.data;
                    globals.insert(globals.end(), p, p + src.globals.size);
                }
            }
            bool Empty() const noexcept {
                return textures.empty() && buffers.empty() && globals.empty();
            }

            void Clear() noexcept { textures.clear(); buffers.clear(); globals.clear(); }
        };

        // Append multiple batches into an existing OwnedBarrierBatch
        inline void AppendBarrierBatches(OwnedBarrierBatch& out,
            Span<const BarrierBatch> srcs) {
            size_t t = 0, b = 0, g = 0;
            for (uint32_t i = 0; i < srcs.size; ++i) {
                const BarrierBatch& s = srcs.data[i];
                t += s.textures.size;
                b += s.buffers.size;
                g += s.globals.size;
            }
            out.textures.reserve(out.textures.size() + t);
            out.buffers.reserve(out.buffers.size() + b);
            out.globals.reserve(out.globals.size() + g);

            // Concatenate ranges
            for (uint32_t i = 0; i < srcs.size; ++i) {
                const BarrierBatch& s = srcs.data[i];

                if (s.textures.size) {
                    const auto* p = s.textures.data;
                    out.textures.insert(out.textures.end(), p, p + s.textures.size);
                }
                if (s.buffers.size) {
                    const auto* p = s.buffers.data;
                    out.buffers.insert(out.buffers.end(), p, p + s.buffers.size);
                }
                if (s.globals.size) {
                    const auto* p = s.globals.data;
                    out.globals.insert(out.globals.end(), p, p + s.globals.size);
                }
            }
        }

        inline OwnedBarrierBatch CombineBarrierBatches(Span<const BarrierBatch> srcs) {
            OwnedBarrierBatch out;
            AppendBarrierBatches(out, srcs);
            return out;
        }

        inline OwnedBarrierBatch CombineBarrierBatches(std::initializer_list<BarrierBatch> srcs) {
            Span<const BarrierBatch> s{ srcs.begin(), static_cast<uint32_t>(srcs.size()) };
            return CombineBarrierBatches(s);
        }

        template<class ContiguousContainer>
        inline OwnedBarrierBatch CombineBarrierBatches(const ContiguousContainer& c) {
            Span<const BarrierBatch> s{ c.data(), static_cast<uint32_t>(c.size()) };
            return CombineBarrierBatches(s);
        }
        inline const char* ResourceLayoutToString(rhi::ResourceLayout layout)
        {
            switch (layout)
            {
            case ResourceLayout::Undefined:               return "UNDEFINED";
            case ResourceLayout::Common:                  return "COMMON";
            case ResourceLayout::Present:                 return "PRESENT";
            case ResourceLayout::GenericRead:             return "GENERIC_READ";
            case ResourceLayout::RenderTarget:            return "RENDER_TARGET";
            case ResourceLayout::UnorderedAccess:         return "UNORDERED_ACCESS";
            case ResourceLayout::DepthReadWrite:          return "DEPTH_STENCIL_WRITE";
            case ResourceLayout::DepthRead:               return "DEPTH_STENCIL_READ";
            case ResourceLayout::ShaderResource:          return "SHADER_RESOURCE";
            case ResourceLayout::CopySource:              return "COPY_SOURCE";
            case ResourceLayout::CopyDest:                return "COPY_DEST";
            case ResourceLayout::ResolveSource:           return "RESOLVE_SOURCE";
            case ResourceLayout::ResolveDest:             return "RESOLVE_DEST";
            case ResourceLayout::ShadingRateSource:       return "SHADING_RATE_SOURCE";

            case ResourceLayout::DirectCommon:            return "DIRECT_QUEUE_COMMON";
            case ResourceLayout::DirectGenericRead:       return "DIRECT_QUEUE_GENERIC_READ";
            case ResourceLayout::DirectUnorderedAccess:   return "DIRECT_QUEUE_UNORDERED_ACCESS";
            case ResourceLayout::DirectShaderResource:    return "DIRECT_QUEUE_SHADER_RESOURCE";
            case ResourceLayout::DirectCopySource:        return "DIRECT_QUEUE_COPY_SOURCE";
            case ResourceLayout::DirectCopyDest:          return "DIRECT_QUEUE_COPY_DEST";

            case ResourceLayout::ComputeCommon:           return "COMPUTE_QUEUE_COMMON";
            case ResourceLayout::ComputeGenericRead:      return "COMPUTE_QUEUE_GENERIC_READ";
            case ResourceLayout::ComputeUnorderedAccess:  return "COMPUTE_QUEUE_UNORDERED_ACCESS";
            case ResourceLayout::ComputeShaderResource:   return "COMPUTE_QUEUE_SHADER_RESOURCE";
            case ResourceLayout::ComputeCopySource:       return "COMPUTE_QUEUE_COPY_SOURCE";
            case ResourceLayout::ComputeCopyDest:         return "COMPUTE_QUEUE_COPY_DEST";
            default:                                      return "UNKNOWN";
            }
        }

        inline std::string ResourceAccessMaskToString(ResourceAccessType mask)
        {
            using U = unsigned int;
            U v = static_cast<U>(mask);
            if (v == 0) return "NONE";

            std::string out;
            auto add = [&](const char* s) {
                if (!out.empty()) out += '|';
                out += s;
                };

            if (v & static_cast<U>(ResourceAccessType::Common))                              add("COMMON");
            if (v & static_cast<U>(ResourceAccessType::VertexBuffer))                        add("VERTEX_BUFFER");
            if (v & static_cast<U>(ResourceAccessType::ConstantBuffer))                      add("CONSTANT_BUFFER");
            if (v & static_cast<U>(ResourceAccessType::IndexBuffer))                         add("INDEX_BUFFER");
            if (v & static_cast<U>(ResourceAccessType::RenderTarget))                        add("RENDER_TARGET");
            if (v & static_cast<U>(ResourceAccessType::UnorderedAccess))                     add("UNORDERED_ACCESS");
            if (v & static_cast<U>(ResourceAccessType::DepthReadWrite))                      add("DEPTH_STENCIL_WRITE");
            if (v & static_cast<U>(ResourceAccessType::DepthRead))                           add("DEPTH_STENCIL_READ");
            if (v & static_cast<U>(ResourceAccessType::ShaderResource))                      add("SHADER_RESOURCE");
            if (v & static_cast<U>(ResourceAccessType::IndirectArgument))                    add("INDIRECT_ARGUMENT");
            if (v & static_cast<U>(ResourceAccessType::CopyDest))                            add("COPY_DEST");
            if (v & static_cast<U>(ResourceAccessType::CopySource))                          add("COPY_SOURCE");
            if (v & static_cast<U>(ResourceAccessType::RaytracingAccelerationStructureRead)) add("RT_AS_READ");
            if (v & static_cast<U>(ResourceAccessType::RaytracingAccelerationStructureWrite))add("RT_AS_WRITE");

            // Any unknown bits?
            if (out.empty()) out = "UNKNOWN";
            return out;
        }

        inline const char* ResourceSyncToString(ResourceSyncState sync)
        {
            switch (sync)
            {
            case ResourceSyncState::None:          return "NONE";
            case ResourceSyncState::All:           return "ALL";
            case ResourceSyncState::Draw:          return "DRAW";
            case ResourceSyncState::IndexInput:    return "INDEX_INPUT";
            case ResourceSyncState::VertexShading: return "VERTEX_SHADING";
            case ResourceSyncState::PixelShading:  return "PIXEL_SHADING";
            case ResourceSyncState::DepthStencil:  return "DEPTH_STENCIL";
            case ResourceSyncState::RenderTarget:  return "RENDER_TARGET";
            case ResourceSyncState::ComputeShading:return "COMPUTE_SHADING";
            case ResourceSyncState::Raytracing:    return "RAYTRACING";
            case ResourceSyncState::Copy:          return "COPY";
            case ResourceSyncState::Resolve:       return "RESOLVE";
            case ResourceSyncState::ExecuteIndirect:return "EXECUTE_INDIRECT";
            case ResourceSyncState::Predication:   return "PREDICATION";
            case ResourceSyncState::AllShading:    return "ALL_SHADING";
            case ResourceSyncState::NonPixelShading:return "NON_PIXEL_SHADING";
            case ResourceSyncState::EmitRaytracingAccelerationStructurePostbuildInfo:
                return "EMIT_RTAS_POSTBUILD_INFO";
            case ResourceSyncState::ClearUnorderedAccessView:
                return "CLEAR_UNORDERED_ACCESS_VIEW";
            case ResourceSyncState::VideoDecode:   return "VIDEO_DECODE";
            case ResourceSyncState::VideoProcess:  return "VIDEO_PROCESS";
            case ResourceSyncState::VideoEncode:   return "VIDEO_ENCODE";
            case ResourceSyncState::BuildRaytracingAccelerationStructure:
                return "BUILD_RAYTRACING_ACCELERATION_STRUCTURE";
            case ResourceSyncState::CopyRatracingAccelerationStructure:
                return "COPY_RAYTRACING_ACCELERATION_STRUCTURE";
            case ResourceSyncState::SyncSplit:     return "SPLIT";
            default:                               return "UNKNOWN";
            }
        }
    } // namespace helpers
} // namespace rhi