#pragma once
#include "rhi.h"

// RHIHelpers.h
#pragma once
#include <utility>     // std::move
#include <type_traits> // std::is_trivially_copyable_v

namespace rhi {
    namespace helpers {
        bool IsTextureResourceType(ResourceType type) noexcept {
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
                Memory memory = Memory::DeviceLocal,
                ResourceFlags flags = {},
                const char* debugName = nullptr) noexcept
            {
                rhi::ResourceDesc d{};
                d.type = ResourceType::Buffer;
                d.flags = flags;
                d.debugName = debugName;
                d.buffer = BufferDesc{ sizeBytes };
                d.memory = memory;
                return d;
            }

            static constexpr ResourceDesc Texture(
				ResourceType type,
                Format format,
                Memory memory,
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
                d.flags = flags;
                d.debugName = debugName;
                d.memory = memory;
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

            static constexpr ResourceDesc Tex2D(Format fmt, Memory memory, uint32_t w, uint32_t h,
                uint16_t mips = 1, uint32_t sampleCount = 1, uint16_t array = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* name = nullptr) noexcept
            {
                return Texture(ResourceType::Texture2D,
                    fmt, memory, w, h, array, mips, sampleCount, initial, clear, flags, name);
            }

            static constexpr ResourceDesc Tex3D(Format fmt, Memory memory, uint32_t w, uint32_t h, uint16_t d,
				uint16_t mips = 1, uint32_t sampleCount = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* name = nullptr) noexcept
            {
                return Texture(ResourceType::Texture3D, fmt, memory, w, h, d, mips, sampleCount, initial, clear, flags, name);
            }

            static constexpr ResourceDesc TexCube(Format fmt, Memory memory, uint32_t edge,
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
            constexpr ResourceDesc& WithFlags(ResourceFlags f) & noexcept { this->flags = f; return *this; }
            constexpr ResourceDesc&& WithFlags(ResourceFlags f) && noexcept { this->flags = f; return std::move(*this); }

            constexpr ResourceDesc& DebugName(const char* n) & noexcept { this->debugName = n; return *this; }
            constexpr ResourceDesc&& DebugName(const char* n) && noexcept { this->debugName = n; return std::move(*this); }

            // Texture-specific tweaks
            constexpr ResourceDesc& InitialLayout(ResourceLayout l) & noexcept
            {
                if (IsTextureResourceType(type)) texture.initialLayout = l;
                return *this;
            }
            constexpr ResourceDesc&& InitialLayout(ResourceLayout l) && noexcept
            {
                if (IsTextureResourceType(type)) texture.initialLayout = l;
                return std::move(*this);
            }
            constexpr ResourceDesc& OptimizedClear(const ClearValue* cv) & noexcept
            {
                if (IsTextureResourceType(type)) texture.optimizedClear = cv;
                return *this;
            }
            constexpr ResourceDesc&& OptimizedClear(const ClearValue* cv) && noexcept
            {
                if (IsTextureResourceType(type)) texture.optimizedClear = cv;
                return std::move(*this);
            }

            // Helpers
            constexpr bool IsBuffer()  const noexcept { return type == ResourceType::Buffer; }
            constexpr bool IsTexture() const noexcept { return IsTextureResourceType(type); }
        };

        static_assert(std::is_trivially_copyable_v<rhi::ResourceDesc>, "Keep ResourceDesc trivially copyable for constexpr friendliness.");

        struct SubresourceData {
            const void* data = nullptr;   // pointer to CPU pixels/bytes for this subresource
            uint64_t    rowPitch = 0;     // bytes per row
            uint64_t    slicePitch = 0;   // bytes per 2D slice (for 3D or array-layer)
        };

        enum UploadFlags : uint32_t {
            Upload_None = 0,
            Upload_ManageBarriers = 1 << 0, // if set, helper will transition dst to CopyDest and back
        };
        inline UploadFlags operator|(UploadFlags a, UploadFlags b) {
            return static_cast<UploadFlags>(uint32_t(a) | uint32_t(b));
        }

        // Query how much space you need in the INTERMEDIATE (upload) buffer.
        // (DX12: GetCopyableFootprints; VK: compute tight/compatible layout)
        uint64_t RequiredUploadBufferSize(Device* dev,
            ResourceHandle dst,
            uint32_t firstSubresource,
            uint32_t numSubresources) noexcept;

        // Record the copy *and* (optionally) write the upload memory.
        // - 'upload' must be a buffer in Upload/HostVisible memory.
        // - 'uploadOffset' is the starting byte offset into that buffer.
        // - 'src' holds per-subresource CPU pointers and pitches.
        // Returns Result::Ok or an error.
        Result UpdateSubresources(CommandList& cl,
            Device* dev,
            ResourceHandle dst,
            ResourceHandle upload,
            uint64_t uploadOffset,
            uint32_t firstSubresource,
            Span<const SubresourceData> src,
            UploadFlags flags = Upload_None) noexcept;
    } // namespace helpers
} // namespace rhi