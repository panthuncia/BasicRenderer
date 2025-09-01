#pragma once
#include "rhi.h"

// RHIHelpers.h
#pragma once
#include <utility>     // std::move
#include <type_traits> // std::is_trivially_copyable_v

namespace rhi {
    namespace helpers {
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

            static constexpr ResourceDesc Texture(TextureViewDim dim,
                Format format,
                Memory memory,
                uint32_t width,
                uint32_t height = 1,
                uint16_t depthOrLayers = 1,
                uint16_t mipLevels = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* debugName = nullptr) noexcept
            {
                ResourceDesc d{};
                d.type = ResourceType::Texture;
                d.flags = flags;
                d.debugName = debugName;
                d.memory = memory;
                d.texture = TextureDesc{
                    /*format*/        format,
                    /*width*/         width,
                    /*height*/        height,
                    /*depthOrLayers*/ depthOrLayers,
                    /*mipLevels*/     mipLevels,
                    /*dim*/           dim,
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
                uint16_t mips = 1, uint16_t array = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* name = nullptr) noexcept
            {
                return Texture(array > 1 ? TextureViewDim::Tex2DArray : TextureViewDim::Tex2D,
                    fmt, memory, w, h, array, mips, initial, clear, flags, name);
            }

            static constexpr ResourceDesc Tex3D(Format fmt, Memory memory, uint32_t w, uint32_t h, uint16_t d,
                uint16_t mips = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* name = nullptr) noexcept
            {
                return Texture(TextureViewDim::Tex3D, fmt, memory, w, h, d, mips, initial, clear, flags, name);
            }

            static constexpr ResourceDesc TexCube(Format fmt, Memory memory, uint32_t edge,
                uint16_t mips = 1, uint16_t cubes = 1,
                ResourceLayout initial = ResourceLayout::Undefined,
                const ClearValue* clear = nullptr,
                ResourceFlags flags = {},
                const char* name = nullptr) noexcept
            {
                const bool isArray = (cubes > 1);
                const uint16_t totalLayers = static_cast<uint16_t>(6 * cubes);
                return Texture(isArray ? TextureViewDim::CubeArray : TextureViewDim::Cube,
                    fmt, memory, edge, edge, totalLayers, mips, initial, clear, flags, name);
            }

            // Light builder API: works with lvalues and rvalues
            constexpr ResourceDesc& WithFlags(ResourceFlags f) & noexcept { this->flags = f; return *this; }
            constexpr ResourceDesc&& WithFlags(ResourceFlags f) && noexcept { this->flags = f; return std::move(*this); }

            constexpr ResourceDesc& DebugName(const char* n) & noexcept { this->debugName = n; return *this; }
            constexpr ResourceDesc&& DebugName(const char* n) && noexcept { this->debugName = n; return std::move(*this); }

            // Texture-specific tweaks
            constexpr ResourceDesc& InitialLayout(ResourceLayout l) & noexcept
            {
                if (type == ResourceType::Texture) texture.initialLayout = l;
                return *this;
            }
            constexpr ResourceDesc&& InitialLayout(ResourceLayout l) && noexcept
            {
                if (type == ResourceType::Texture) texture.initialLayout = l;
                return std::move(*this);
            }
            constexpr ResourceDesc& OptimizedClear(const ClearValue* cv) & noexcept
            {
                if (type == ResourceType::Texture) texture.optimizedClear = cv;
                return *this;
            }
            constexpr ResourceDesc&& OptimizedClear(const ClearValue* cv) && noexcept
            {
                if (type == ResourceType::Texture) texture.optimizedClear = cv;
                return std::move(*this);
            }

            // Helpers
            constexpr bool IsBuffer()  const noexcept { return type == ResourceType::Buffer; }
            constexpr bool IsTexture() const noexcept { return type == ResourceType::Texture; }
        };

        static_assert(std::is_trivially_copyable_v<rhi::ResourceDesc>, "Keep ResourceDesc trivially copyable for constexpr friendliness.");
    } // namespace helpers
} // namespace rhi