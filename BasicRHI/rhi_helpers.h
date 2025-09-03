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
                rhi::helpers::ResourceDesc d{};
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
            default: return 4; // conservative default
            }
        }

        static inline uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + (a - 1)) & ~(a - 1); }
        static inline uint64_t AlignUp64(uint64_t v, uint64_t a) { return (v + (a - 1)) & ~(a - 1); }

        struct SubresourceData {
            const void* pData = nullptr;
            uint32_t    rowPitch = 0;   // bytes in source
            uint32_t    slicePitch = 0; // bytes in source (rowPitch * rows for 2D)
        };

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
            const bool bc = IsBlockCompressed(fmt);
            const uint32_t blockW = bc ? 4u : 1u;
            const uint32_t blockH = bc ? 4u : 1u;
            const uint32_t bytesPerBlock = BytesPerBlock(fmt);

            // Safe cross-API alignment
            constexpr uint32_t RowPitchAlign = 256;  // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
            constexpr uint64_t PlacementAlign = 512; // D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT

            // Plan buffer size and individual placed footprints
            struct Footprint {
                uint64_t offset;
                uint32_t rowPitch;   // destination row pitch in upload buffer
                uint32_t slicePitch; // destination slice pitch in upload buffer
                uint32_t width, height, depth; // in texels (per mip)
                uint32_t mip, arraySlice, zSlice;
                bool     valid;       // whether we have data to copy
            };
            std::vector<Footprint> fps;
            fps.reserve(arraySize * mipLevels * depthOrLayers);

            uint64_t totalSize = 0;
            auto idxOf = [&](uint32_t a, uint32_t m, uint32_t z) {
                return (size_t(a) * mipLevels + m) * depthOrLayers + z;
                };

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

                    const uint32_t depthSlices = depthOrLayers; // for 2D array/cube, this is 1 per subresource (handled by arraySize)

                    for (uint32_t z = 0; z < depthSlices; ++z) {
                        Footprint fp{};
                        fp.offset = AlignUp64(totalSize, PlacementAlign);
                        fp.rowPitch = rowPitch;
                        fp.slicePitch = slicePitch;
                        fp.width = mipW; fp.height = mipH; fp.depth = 1;
                        fp.mip = m; fp.arraySlice = a; fp.zSlice = z;

                        const size_t srcIdx = idxOf(a, m, z);
                        fp.valid = (srcIdx < srcSubresources.size) && (srcSubresources.data[srcIdx].pData != nullptr);

                        totalSize = fp.offset + fp.slicePitch; // for 2D/cube
                        fps.push_back(fp);
                    }
                }
            }

            if (totalSize == 0) {
                return {}; // nothing to upload
            }

            // Create UPLOAD buffer
            ResourceDesc upDesc{};
            upDesc.type = ResourceType::Buffer;
            upDesc.memory = Memory::Upload;
            upDesc.flags = ResourceFlags::None;
            upDesc.buffer.sizeBytes = totalSize;
            upDesc.debugName = "TextureUpload";
            auto upload = dev.CreateCommittedResource(upDesc);
            if (!upload) return {};

            // Map once, copy rows for each valid subresource
            void* mapped = nullptr;
            upload->Map(&mapped, 0, totalSize);
            uint8_t* dstBase = static_cast<uint8_t*>(mapped);

            for (const auto& fp : fps) {
                const size_t srcIdx = idxOf(fp.arraySlice, fp.mip, fp.zSlice);
                if (!fp.valid) continue;
                const auto& src = srcSubresources.data[srcIdx];

                const bool srcCompressed = bc; // assume source matches fmt
                const uint32_t rows = srcCompressed ? ((fp.height + (blockH - 1)) / blockH) : fp.height;
                const uint32_t rowSize = srcCompressed
                    ? (((fp.width + (blockW - 1)) / blockW) * bytesPerBlock)
                    : (fp.width * bytesPerBlock);

                const uint8_t* srcPtr = static_cast<const uint8_t*>(src.pData);
                uint8_t* dstPtr = dstBase + fp.offset;

                for (uint32_t r = 0; r < rows; ++r) {
                    const uint8_t* s = srcPtr + size_t(r) * src.rowPitch;
                    uint8_t* d = dstPtr + size_t(r) * fp.rowPitch;
                    std::memcpy(d, s, rowSize);
                }
                // TODO: for 3D, repeat per z-slice into subsequent slicePitch regions
            }

			upload->Unmap(0, 0); // Full range

            // Record GPU copies: one per subresource (or Z slice)
            for (const auto& fp : fps) {
                if (!fp.valid) continue;
                BufferTextureCopy srcBT{};
                srcBT.buffer = upload->GetHandle();
                srcBT.offset = fp.offset;
                srcBT.rowPitch = fp.rowPitch;
                srcBT.slicePitch = fp.slicePitch;

                TextureCopyRegion dstReg{};
                dstReg.texture = dstTexture.GetHandle();
                dstReg.mip = fp.mip;
                dstReg.arraySlice = fp.arraySlice;
                dstReg.x = 0; dstReg.y = 0; dstReg.z = fp.zSlice;
                dstReg.width = fp.width;
                dstReg.height = fp.height;
                dstReg.depth = 1;

                cl.CopyBufferToTexture(dstReg, srcBT);
            }

            return upload; // keep alive until GPU finishes (caller can fence/wait)
        }
    } // namespace helpers
} // namespace rhi