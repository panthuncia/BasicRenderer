#include "Factories/TextureFactory.h"

#include <algorithm>
#include <stdexcept>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <OpenRenderGraph/OpenRenderGraph.h>

#include "Managers/Singletons/TextureProcessingManager.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Sampler.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/ReadbackRequest.h"
#include "Render/Runtime/IReadbackService.h"
#include "ThirdParty/stb/stb_image.h"
#include "rhi_helpers.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"

#define A_CPU
#include "../shaders/FidelityFX/ffx_a.h"
#include "../shaders/FidelityFX/ffx_spd.h"


namespace {
    void UploadTextureData(
        const std::shared_ptr<Resource>& dstTexture,
        const TextureDescription& desc,
        const std::vector<std::shared_ptr<std::vector<uint8_t>>>& initialData,
        unsigned int mipLevels)
    {
        if (initialData.empty()) return;

        const uint32_t faces = desc.isCubemap ? 6u : 1u;
        const uint32_t arraySlices = faces * static_cast<uint32_t>(desc.arraySize);
        const uint32_t numSubres = arraySlices * static_cast<uint32_t>(mipLevels);

#if BUILD_TYPE == BUILD_TYPE_DEBUG
        if (desc.imageDimensions.size() < numSubres) {
            throw std::runtime_error("UploadTextureData: desc.imageDimensions is smaller than expected subresource count.");
        }
#endif

        // Dense SubresourceData table (nullptr entries allowed; skipped by uploader)
        std::vector<rhi::helpers::SubresourceData> srd(numSubres);

        // Keep any expanded buffers alive until upload helper returns
        std::vector<std::vector<stbi_uc>> expandedImages;
        expandedImages.reserve(numSubres);

        // Pad/trim caller-provided data to full subresource count.
        // This also pins shared_ptr lifetimes through the upload call.
        std::vector<std::shared_ptr<std::vector<uint8_t>>> fullInitial(numSubres, nullptr);
        {
            const size_t toCopy = std::min<size_t>(initialData.size(), static_cast<size_t>(numSubres));
            std::copy_n(initialData.begin(), toCopy, fullInitial.begin());
        }

        const uint32_t baseW = static_cast<uint32_t>(desc.imageDimensions[0].width);
        const uint32_t baseH = static_cast<uint32_t>(desc.imageDimensions[0].height);

        auto RawMatchesStoredPitches = [](uint32_t w, uint32_t h, uint32_t ch,
            size_t storedRowPitch, size_t storedSlicePitch) -> bool
            {
                const size_t rawRow = static_cast<size_t>(w) * static_cast<size_t>(ch);
                const size_t rawSlice = rawRow * static_cast<size_t>(h);
                return (rawRow == storedRowPitch) && (rawSlice == storedSlicePitch);
            };

        for (uint32_t a = 0; a < arraySlices; ++a) {
            for (uint32_t m = 0; m < static_cast<uint32_t>(mipLevels); ++m) {
                const uint32_t subIdx = m + a * static_cast<uint32_t>(mipLevels);

                const auto& dims = desc.imageDimensions[subIdx];
                const size_t storedRowPitch = static_cast<size_t>(dims.rowPitch);
                const size_t storedSlicePitch = static_cast<size_t>(dims.slicePitch);

                const auto& buf = fullInitial[subIdx];
                const stbi_uc* imageData = buf ? reinterpret_cast<const stbi_uc*>(buf->data()) : nullptr;
                const size_t imageBytes = buf ? buf->size() : 0;

                auto& out = srd[subIdx];

                if (!imageData) {
                    out.pData = nullptr;
                    out.rowPitch = out.slicePitch = 0;
                    continue;
                }

#if BUILD_TYPE == BUILD_TYPE_DEBUG
                if (imageBytes < storedSlicePitch) {
                    throw std::runtime_error("UploadTextureData: subresource buffer smaller than expected slicePitch.");
                }
#endif

                uint32_t channels = desc.channels;

                // Pick a width/height that makes "raw tightly packed" match the stored pitches, if possible.
                // This keeps behavior compatible whether imageDimensions stores per-mip sizes or base sizes.
                uint32_t w = static_cast<uint32_t>(dims.width);
                uint32_t h = static_cast<uint32_t>(dims.height);

                bool rawPacked =
                    RawMatchesStoredPitches(w, h, channels, storedRowPitch, storedSlicePitch);

                if (!rawPacked) {
                    const uint32_t w2 = std::max(1u, w >> m);
                    const uint32_t h2 = std::max(1u, h >> m);
                    rawPacked = RawMatchesStoredPitches(w2, h2, channels, storedRowPitch, storedSlicePitch);
                    if (rawPacked) { w = w2; h = h2; }
                }

                if (!rawPacked) {
                    const uint32_t w3 = std::max(1u, baseW >> m);
                    const uint32_t h3 = std::max(1u, baseH >> m);
                    rawPacked = RawMatchesStoredPitches(w3, h3, channels, storedRowPitch, storedSlicePitch);
                    if (rawPacked) { w = w3; h = h3; }
                }

                if (!rawPacked) {
                    // Pre-padded / compressed / otherwise non-raw layout: pass pitches through unchanged.
                    out.pData = imageData;
                    out.rowPitch = static_cast<uint32_t>(storedRowPitch);
                    out.slicePitch = static_cast<uint32_t>(storedSlicePitch);
                    continue;
                }

                // Tightly packed: optionally expand RGB -> RGBA for upload convenience.
                const stbi_uc* ptr = imageData;
                if (channels == 3) {
                    expandedImages.emplace_back(ExpandImageData(imageData, w, h)); // returns RGBA8
                    ptr = expandedImages.back().data();
                    channels = 4;
                }

                out.pData = ptr;
                out.rowPitch = w * channels;
                out.slicePitch = out.rowPitch * h;
            }
        }

        auto device = DeviceManager::GetInstance().GetDevice();

        TEXTURE_UPLOAD_SUBRESOURCES(
            rg::runtime::UploadTarget::FromShared(dstTexture),
            desc.format,
            baseW,
            baseH,
            /*depthOrLayers*/ 1,
            static_cast<uint32_t>(mipLevels),
            arraySlices,
            srd.data(),
            static_cast<uint32_t>(srd.size()));
    }
}

namespace {

    namespace detail
    {
        float Unorm8ToFloat(uint8_t v) noexcept
        {
            return float(v) * (1.0f / 255.0f);
        }

        uint8_t FloatToUnorm8(float x) noexcept
        {
            x = std::clamp(x, 0.0f, 1.0f);
            // round-to-nearest
            const int v = int(x * 255.0f + 0.5f);
            return static_cast<uint8_t>(std::clamp(v, 0, 255));
        }

        // sRGB <-> Linear conversion for scalar in [0,1].
        float SrgbToLinear(float c) noexcept
        {
            if (c <= 0.04045f) return c / 12.92f;
            return std::pow((c + 0.055f) / 1.055f, 2.4f);
        }

        float LinearToSrgb(float c) noexcept
        {
            c = std::clamp(c, 0.0f, 1.0f);
            if (c <= 0.0031308f) return 12.92f * c;
            return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        }

        // LUT for 8-bit sRGB -> linear float to avoid pow() on decode.
        const std::array<float, 256>& Srgb8ToLinearLUT()
        {
            static const std::array<float, 256> lut = [] {
                std::array<float, 256> t{};
                for (int i = 0; i < 256; ++i) {
                    const float s = float(i) * (1.0f / 255.0f);
                    t[size_t(i)] = SrgbToLinear(s);
                }
                return t;
                }();
            return lut;
        }

        float Srgb8ToLinear(uint8_t v) noexcept
        {
            return Srgb8ToLinearLUT()[size_t(v)];
        }

        uint8_t LinearToSrgb8(float linear) noexcept
        {
            return FloatToUnorm8(LinearToSrgb(linear));
        }
    }

    std::vector<std::shared_ptr<std::vector<uint8_t>>> BuildMipChain2D(
        const std::shared_ptr<std::vector<uint8_t>>& base,
        uint32_t baseW,
        uint32_t baseH,
        uint32_t channels,
        uint32_t mipLevels,
        bool isSRGB,
        bool premultiplyAlpha = true)
    {
        if (!base) throw std::runtime_error("BuildMipChain2D: null base data.");
        if (channels == 0 || channels > 4) {
            throw std::runtime_error("BuildMipChain2D: unsupported channel count (expected 1..4).");
        }
        if (mipLevels == 0) {
            throw std::runtime_error("BuildMipChain2D: mipLevels must be > 0.");
        }

        const size_t expectedBaseBytes = static_cast<size_t>(baseW) * baseH * channels;
        if (base->size() < expectedBaseBytes) {
            throw std::runtime_error("BuildMipChain2D: base data buffer is smaller than width*height*channels.");
        }

        // If premultiplyAlpha is requested, require an alpha channel
        if (premultiplyAlpha && channels < 2) {
            premultiplyAlpha = false;
        }

        std::vector<std::shared_ptr<std::vector<uint8_t>>> chain;
        chain.reserve(mipLevels);
        chain.push_back(base);

        uint32_t prevW = baseW;
        uint32_t prevH = baseH;

        auto decodeColor = [&](uint8_t v) noexcept -> float {
            // Interpret color channels as sRGB if isSRGB, otherwise treat as linear UNORM.
            return isSRGB ? detail::Srgb8ToLinear(v) : detail::Unorm8ToFloat(v);
            };
        auto encodeColor = [&](float linear) noexcept -> uint8_t {
            // Store back as sRGB if isSRGB, otherwise store as linear UNORM.
            return isSRGB ? detail::LinearToSrgb8(linear) : detail::FloatToUnorm8(linear);
            };

        for (uint32_t level = 1; level < mipLevels; ++level) {
            const uint32_t w = std::max(1u, prevW >> 1);
            const uint32_t h = std::max(1u, prevH >> 1);

            auto dst = std::make_shared<std::vector<uint8_t>>();
            dst->resize(static_cast<size_t>(w) * h * channels);

            const uint8_t* src = chain.back()->data();

            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    const uint32_t sx0 = std::min(prevW - 1, x * 2 + 0);
                    const uint32_t sx1 = std::min(prevW - 1, x * 2 + 1);
                    const uint32_t sy0 = std::min(prevH - 1, y * 2 + 0);
                    const uint32_t sy1 = std::min(prevH - 1, y * 2 + 1);

                    const uint32_t idx00 = (sy0 * prevW + sx0) * channels;
                    const uint32_t idx10 = (sy0 * prevW + sx1) * channels;
                    const uint32_t idx01 = (sy1 * prevW + sx0) * channels;
                    const uint32_t idx11 = (sy1 * prevW + sx1) * channels;

                    const uint32_t dstIdx = (y * w + x) * channels;

                    // Accumulate in linear floats.
                    // Supports:
                    //  - channels==1: treat channel 0 as "color" (sRGB if isSRGB)
                    //  - channels==2: treat channel 0 as "color", channel 1 as alpha (linear)
                    //  - channels==3: RGB color
                    //  - channels==4: RGBA color (alpha linear)
                    float acc[4] = { 0, 0, 0, 0 };

                    auto accumulateSample = [&](uint32_t srcIdx)
                        {
                            float a = 1.0f;
                            if (channels == 2) {
                                a = detail::Unorm8ToFloat(src[srcIdx + 1]);
                            }
                            else if (channels == 4) {
                                a = detail::Unorm8ToFloat(src[srcIdx + 3]);
                            }

                            // Decode color channels to linear
                            float c0 = (channels >= 1) ? decodeColor(src[srcIdx + 0]) : 0.0f;
                            float c1 = (channels >= 3) ? decodeColor(src[srcIdx + 1]) : 0.0f;
                            float c2 = (channels >= 3) ? decodeColor(src[srcIdx + 2]) : 0.0f;

                            if (premultiplyAlpha && (channels == 2 || channels == 4)) {
                                c0 *= a;
                                c1 *= a;
                                c2 *= a;
                            }

                            // Accumulate
                            acc[0] += c0;
                            if (channels >= 3) {
                                acc[1] += c1;
                                acc[2] += c2;
                            }
                            if (channels == 2) {
                                acc[1] += a;
                            }
                            else if (channels == 4) {
                                acc[3] += a;
                            }
                        };

                    accumulateSample(idx00);
                    accumulateSample(idx10);
                    accumulateSample(idx01);
                    accumulateSample(idx11);

                    const float inv4 = 0.25f;
                    // Average
                    acc[0] *= inv4;
                    acc[1] *= inv4;
                    acc[2] *= inv4;
                    acc[3] *= inv4;

                    // Unpremultiply if needed
                    if (premultiplyAlpha && channels == 4) {
                        const float a = acc[3];
                        if (a > 0.0f) {
                            acc[0] /= a;
                            acc[1] /= a;
                            acc[2] /= a;
                        }
                    }
                    if (premultiplyAlpha && channels == 2) {
                        const float a = acc[1];
                        if (a > 0.0f) {
                            acc[0] /= a;
                        }
                    }

                    // Write out
                    if (channels == 1) {
                        (*dst)[dstIdx + 0] = encodeColor(acc[0]);
                    }
                    else if (channels == 2) {
                        (*dst)[dstIdx + 0] = encodeColor(acc[0]);               // luma/color
                        (*dst)[dstIdx + 1] = detail::FloatToUnorm8(acc[1]);     // alpha
                    }
                    else if (channels == 3) {
                        (*dst)[dstIdx + 0] = encodeColor(acc[0]);
                        (*dst)[dstIdx + 1] = encodeColor(acc[1]);
                        (*dst)[dstIdx + 2] = encodeColor(acc[2]);
                    }
                    else { // channels == 4
                        (*dst)[dstIdx + 0] = encodeColor(acc[0]);
                        (*dst)[dstIdx + 1] = encodeColor(acc[1]);
                        (*dst)[dstIdx + 2] = encodeColor(acc[2]);
                        (*dst)[dstIdx + 3] = detail::FloatToUnorm8(acc[3]);     // alpha stays linear
                    }
                }
            }

            chain.push_back(std::move(dst));
            prevW = w;
            prevH = h;
        }

        return chain;
    }

    uint32_t CalcMipCount(uint32_t w, uint32_t h) noexcept {
        uint32_t levels = 1;
        while (w > 1 || h > 1) {
            w = std::max(1u, w >> 1);
            h = std::max(1u, h >> 1);
            ++levels;
        }
        return levels;
    }

    std::shared_ptr<Buffer> CreateRawByteAddressBuffer(uint64_t bufferSize, bool unorderedAccess, std::string_view debugName)
    {
        auto buffer = Buffer::CreateSharedUnmaterialized(rhi::HeapType::DeviceLocal, bufferSize, unorderedAccess);

        BufferBase::DescriptorRequirements requirements{};
        requirements.createSRV = true;
        requirements.createUAV = unorderedAccess;
        requirements.srvDesc = rhi::SrvDesc{
            .dimension = rhi::SrvDim::Buffer,
            .formatOverride = rhi::Format::R32_Typeless,
            .buffer = {
                .kind = rhi::BufferViewKind::Raw,
                .firstElement = 0,
                .numElements = static_cast<uint32_t>(bufferSize / 4u),
                .structureByteStride = 0,
            },
        };
        requirements.uavDesc = rhi::UavDesc{
            .dimension = rhi::UavDim::Buffer,
            .formatOverride = rhi::Format::R32_Typeless,
            .buffer = {
                .kind = rhi::BufferViewKind::Raw,
                .firstElement = 0,
                .numElements = static_cast<uint32_t>(bufferSize / 4u),
                .structureByteStride = 0,
                .counterOffsetInBytes = 0,
            },
        };

        buffer->SetDescriptorRequirements(requirements);
        buffer->Materialize();
        if (!debugName.empty()) {
            buffer->SetName(std::string(debugName));
        }
        return buffer;
    }

    TextureDescription BuildBc7CompressedDescription(const TextureSourceData& preparedSourceData, const TextureFileMeta& meta)
    {
        TextureDescription desc = preparedSourceData.desc;
        desc.format = meta.preferSRGB ? rhi::Format::BC7_UNorm_sRGB : rhi::Format::BC7_UNorm;
        desc.channels = 4;
        desc.generateMipMaps = false;
        desc.hasUAV = false;
        desc.hasNonShaderVisibleUAV = false;
        desc.uavFormat = rhi::Format::Unknown;

        for (auto& dims : desc.imageDimensions) {
            const uint32_t blocksWide = (dims.width + 3u) / 4u;
            const uint32_t blocksHigh = (dims.height + 3u) / 4u;
            dims.rowPitch = static_cast<uint64_t>(blocksWide) * 16u;
            dims.slicePitch = dims.rowPitch * blocksHigh;
        }

        return desc;
    }

    std::vector<rhi::CopyableFootprint> BuildBc7CompressionFootprints(const TextureDescription& desc, uint64_t& totalBytes)
    {
        if (desc.imageDimensions.empty()) {
            throw std::runtime_error("BuildBc7CompressionSubresources: texture description has no image dimensions");
        }

        std::vector<rhi::helpers::SubresourceData> dummySubresources(desc.imageDimensions.size());
        for (auto& subresource : dummySubresources) {
            subresource.pData = reinterpret_cast<const void*>(1);
        }

        const uint32_t mipLevels = static_cast<uint32_t>(desc.imageDimensions.size());
        const rhi::Span<const rhi::helpers::SubresourceData> srcSpan{ dummySubresources.data(), static_cast<uint32_t>(dummySubresources.size()) };
        const auto plan = rhi::helpers::PlanTextureUploadSubresources(
            desc.format,
            desc.imageDimensions[0].width,
            desc.imageDimensions[0].height,
            1,
            mipLevels,
            1,
            srcSpan);

        std::vector<rhi::CopyableFootprint> footprints;
        footprints.reserve(plan.footprints.size());
        for (size_t index = 0; index < plan.footprints.size(); ++index) {
            const auto& footprint = plan.footprints[index];

            rhi::CopyableFootprint copyableFootprint{};
            copyableFootprint.offset = footprint.offset;
            copyableFootprint.rowPitch = footprint.rowPitch;
            copyableFootprint.width = footprint.width;
            copyableFootprint.height = footprint.height;
            copyableFootprint.depth = footprint.depth;
            footprints.push_back(copyableFootprint);
        }

        totalBytes = plan.totalSize;
        return footprints;
    }

    std::shared_ptr<TextureSourceData> BuildCompressedSourceDataFromReadback(
        const TextureDescription& desc,
        bool hasFullMipChain,
        const ReadbackCaptureResult& readback)
    {
        if (readback.layouts.size() != desc.imageDimensions.size()) {
            throw std::runtime_error("BuildCompressedSourceDataFromReadback: readback layout count does not match texture subresources");
        }

        auto result = std::make_shared<TextureSourceData>();
        result->desc = desc;
        result->hasFullMipChain = hasFullMipChain;
        result->isBlockCompressed = true;
        result->subresources.reserve(desc.imageDimensions.size());

        for (size_t index = 0; index < desc.imageDimensions.size(); ++index) {
            const auto& dims = desc.imageDimensions[index];
            const auto& footprint = readback.layouts[index];
            const uint32_t rows = rhi::helpers::IsBlockCompressed(desc.format)
                ? (dims.height + 3u) / 4u
                : dims.height;

            const uint64_t srcEnd = footprint.offset + static_cast<uint64_t>(footprint.rowPitch) * rows;
            if (srcEnd > readback.data.size()) {
                throw std::runtime_error("BuildCompressedSourceDataFromReadback: readback buffer is smaller than expected");
            }

            auto bytes = std::make_shared<std::vector<uint8_t>>();
            bytes->resize(static_cast<size_t>(dims.slicePitch));

            const auto* srcBase = reinterpret_cast<const uint8_t*>(readback.data.data()) + footprint.offset;
            for (uint32_t row = 0; row < rows; ++row) {
                std::memcpy(
                    bytes->data() + static_cast<size_t>(row) * dims.rowPitch,
                    srcBase + static_cast<size_t>(row) * footprint.rowPitch,
                    static_cast<size_t>(dims.rowPitch));
            }

            result->subresources.push_back(std::move(bytes));
        }

        return result;
    }
}

std::shared_ptr<PixelBuffer> TextureFactory::CreateAlwaysResidentPixelBuffer(
    TextureDescription desc,
    TextureInitialData initialData,
    std::string_view debugName)
    const {
    if (initialData.Empty()) {
        throw std::runtime_error("CreateAlwaysResidentPixelBuffer: initialData is empty. Use PixelBuffer::CreateShared for data-less textures.");
    }
    if (desc.imageDimensions.empty()) {
        throw std::runtime_error("CreateAlwaysResidentPixelBuffer: desc.imageDimensions must contain at least the base level dimensions.");
    }
    if (desc.channels == 0) {
        throw std::runtime_error("CreateAlwaysResidentPixelBuffer: desc.channels must be set.");
    }

    const uint32_t baseW = desc.imageDimensions[0].width;
    const uint32_t baseH = desc.imageDimensions[0].height;

    const bool doMipmapping = desc.generateMipMaps && !rhi::helpers::IsBlockCompressed(desc.format); // TODO: BC mip gen

    // if caller asked for mipmaps but only provided mip0 for a single-slice 2D texture, generate full chain on CPU.
    if (doMipmapping) {
        // Build full imageDimensions for *all* subresources so UploadTextureData can compute pitches safely.
        const uint32_t faces = desc.isCubemap ? 6u : 1u;
        const uint32_t slices = faces * uint32_t(desc.arraySize);
        const uint32_t mipLevels = CalcMipCount(baseW, baseH);

        desc.imageDimensions.resize(size_t(slices) * mipLevels);
        for (uint32_t s = 0; s < slices; ++s) {
            for (uint32_t m = 0; m < mipLevels; ++m) {
                const uint32_t w = (std::max)(1u, baseW >> m);
                const uint32_t h = (std::max)(1u, baseH >> m);
                const uint32_t idx = m + s * mipLevels;

                desc.imageDimensions[idx].width = w;
                desc.imageDimensions[idx].height = h;
                desc.imageDimensions[idx].rowPitch = uint64_t(w) * desc.channels;
                desc.imageDimensions[idx].slicePitch = desc.imageDimensions[idx].rowPitch * h;
            }
        }

        // Expand initialData to [slice0 mip0.., slice1 mip0..] with only mip0 filled.
        TextureInitialData gpuInit;
        gpuInit.subresources.assign(size_t(slices) * mipLevels, nullptr);

        if (initialData.subresources.size() == 1) {
            gpuInit.subresources[0] = initialData.subresources[0];
        }
        else {
            // If caller provided mip0 for each slice, copy those
            for (uint32_t s = 0; s < slices && s < initialData.subresources.size(); ++s) {
                gpuInit.subresources[s * mipLevels + 0] = initialData.subresources[s];
            }
        }

        initialData = std::move(gpuInit);
    }

    if (doMipmapping) {
        desc.hasUAV = true; // need UAV mips for GPU mipgen
        if (rhi::helpers::IsSRGB(desc.format)) {
            desc.uavFormat = rhi::Format::Unknown;
        }
    }
    auto pb = PixelBuffer::CreateShared(desc);

    if (!debugName.empty()) {
        pb->SetName(std::string(debugName));
    }

    UploadTextureData(pb, desc, initialData.subresources, pb->GetMipLevels());

    // Enqueue GPU mipgen (only if mipLevels > 1)
    if (doMipmapping && pb->GetMipLevels() > 1) {
        const bool isSrgb = rhi::helpers::IsSRGB(desc.format);
        std::static_pointer_cast<MipmappingPass>(m_mipmappingPass)->EnqueueJob(pb, isSrgb);
    }

    return pb;
}

void TextureFactory::SetReadbackService(rg::runtime::IReadbackService* readbackService)
{
    std::static_pointer_cast<BC7CompressionReadbackPass>(m_bc7CompressionReadbackPass)->SetReadbackService(readbackService);
}

bool TextureFactory::SubmitBC7CompressionJob(
    const std::shared_ptr<TextureProcessingJobHandle>& handle,
    std::string_view debugName) const
{
    if (!handle) {
        return false;
    }

    std::shared_ptr<TextureSourceData> preparedSourceData;
    TextureFileMeta requestMeta;
    std::string fallbackName;
    {
        std::scoped_lock lock(handle->mutex);
        preparedSourceData = handle->preparedSourceData;
        requestMeta = handle->requestMeta;
        fallbackName = handle->processingKey;
    }

    if (!preparedSourceData || preparedSourceData->desc.imageDimensions.empty()) {
        return false;
    }

    if (preparedSourceData->isBlockCompressed || preparedSourceData->desc.isArray || preparedSourceData->desc.isCubemap) {
        return false;
    }

    auto* readbackPass = std::static_pointer_cast<BC7CompressionReadbackPass>(m_bc7CompressionReadbackPass).get();
    if (!readbackPass) {
        return false;
    }

    TextureDescription workingDesc = preparedSourceData->desc;
    workingDesc.format = rhi::helpers::stripSrgb(workingDesc.format);
    workingDesc.generateMipMaps = false;
    workingDesc.hasUAV = false;
    workingDesc.uavFormat = rhi::Format::Unknown;

    const std::string jobName = debugName.empty() ? fallbackName : std::string(debugName);
    auto workingTexture = CreateAlwaysResidentPixelBuffer(
        workingDesc,
        TextureInitialData::FromBytes(preparedSourceData->subresources),
        jobName.empty() ? std::string_view("Texture[BC7Working]") : std::string_view(jobName));

    const uint32_t expectedMipLevels = static_cast<uint32_t>(preparedSourceData->desc.imageDimensions.size());
    if (workingTexture->GetMipLevels() != expectedMipLevels) {
        spdlog::error(
            "TextureFactory: BC7 working texture mip mismatch for '{}': expected {} mips but resource created {}",
            jobName,
            expectedMipLevels,
            workingTexture->GetMipLevels());
        return false;
    }

    TextureDescription compressedDesc = BuildBc7CompressedDescription(*preparedSourceData, requestMeta);
    auto compressedTexture = PixelBuffer::CreateShared(compressedDesc);
    if (!jobName.empty()) {
        compressedTexture->SetName(jobName + "[BC7]");
    }

    if (compressedTexture->GetMipLevels() != expectedMipLevels) {
        spdlog::error(
            "TextureFactory: BC7 compressed texture mip mismatch for '{}': expected {} mips but resource created {}",
            jobName,
            expectedMipLevels,
            compressedTexture->GetMipLevels());
        return false;
    }

    uint64_t outputByteSize = 0;
    auto footprints = BuildBc7CompressionFootprints(compressedDesc, outputByteSize);
    auto blockBuffer = CreateRawByteAddressBuffer(
        outputByteSize,
        true,
        jobName.empty() ? std::string_view("Texture[BC7Blocks]") : std::string_view(jobName + "[BC7Blocks]"));

    auto job = std::make_shared<BC7CompressionJob>();
    job->debugName = jobName;
    job->handle = handle;
    job->workingTexture = std::move(workingTexture);
    job->compressedTexture = std::move(compressedTexture);
    job->blockBuffer = std::move(blockBuffer);
    job->subresources.reserve(footprints.size());
    for (uint32_t mip = 0; mip < static_cast<uint32_t>(footprints.size()); ++mip) {
        BC7CompressionSubresource subresource{};
        subresource.footprint = footprints[mip];
        subresource.mip = mip;
        subresource.slice = 0;
        job->subresources.push_back(subresource);
    }
    job->outputByteSize = outputByteSize;

    std::static_pointer_cast<BC7CompressionPass>(m_bc7CompressionPass)->EnqueueJob(job);
    std::static_pointer_cast<BC7CompressionCopyPass>(m_bc7CompressionCopyPass)->EnqueueJob(job);
    readbackPass->EnqueueJob(job);
    return true;
}

bool TextureFactory::MipmappingPass::TryGetValueType(const PixelBuffer& tex, MipmapValueType& outValueType)
{
    const auto& desc = tex.GetDescription();
    const rhi::Format format = desc.uavFormat != rhi::Format::Unknown
        ? desc.uavFormat
        : rhi::helpers::stripSrgb(desc.format);

    switch (format) {
    case rhi::Format::R8_UNorm:
    case rhi::Format::R8_SNorm:
    case rhi::Format::R16_Float:
    case rhi::Format::R16_UNorm:
    case rhi::Format::R16_SNorm:
    case rhi::Format::R32_Float:
        outValueType = MipmapValueType::Float1;
        return true;

    case rhi::Format::R8G8_UNorm:
    case rhi::Format::R8G8_SNorm:
    case rhi::Format::R16G16_Float:
    case rhi::Format::R16G16_UNorm:
    case rhi::Format::R16G16_SNorm:
    case rhi::Format::R32G32_Float:
        outValueType = MipmapValueType::Float2;
        return true;

    case rhi::Format::R8G8B8A8_UNorm:
    case rhi::Format::R8G8B8A8_SNorm:
    case rhi::Format::R16G16B16A16_Float:
    case rhi::Format::R16G16B16A16_UNorm:
    case rhi::Format::R16G16B16A16_SNorm:
    case rhi::Format::R32G32B32A32_Float:
        outValueType = MipmapValueType::Float4;
        return true;

    default:
        return false;
    }
}

PipelineState TextureFactory::MipmappingPass::CreatePipeline(MipmapValueType valueType, bool isArray) const
{
    auto& psoManager = PSOManager::GetInstance();
    auto& layout = psoManager.GetComputeRootSignature();

    std::vector<DxcDefine> defines;
    if (valueType == MipmapValueType::Float1) {
        defines.push_back(DxcDefine{ L"MIPMAP_FLOAT1", L"1" });
    }
    else if (valueType == MipmapValueType::Float2) {
        defines.push_back(DxcDefine{ L"MIPMAP_FLOAT2", L"1" });
    }

    if (isArray) {
        defines.push_back(DxcDefine{ L"MIPMAP_ARRAY", L"1" });
    }

    const char* debugName = nullptr;
    switch (valueType) {
    case MipmapValueType::Float1:
        debugName = isArray ? "MipmapSPD[Float1Array]" : "MipmapSPD[Float12D]";
        break;
    case MipmapValueType::Float2:
        debugName = isArray ? "MipmapSPD[Float2Array]" : "MipmapSPD[Float22D]";
        break;
    case MipmapValueType::Float4:
        debugName = isArray ? "MipmapSPD[Float4Array]" : "MipmapSPD[Float42D]";
        break;
    }

    return psoManager.MakeComputePipeline(
        layout.GetHandle(),
        L"shaders/Utilities/mipmapping.hlsl",
        L"MipmapCSMain",
        std::move(defines),
        debugName);
}

PipelineState& TextureFactory::MipmappingPass::GetOrCreatePipeline(MipmapValueType valueType, bool isArray)
{
    switch (valueType) {
    case MipmapValueType::Float1:
        if (isArray) {
            if (!m_hasPsoFloat1_Array) {
                m_psoFloat1_Array = CreatePipeline(valueType, true);
                m_hasPsoFloat1_Array = true;
            }
            return m_psoFloat1_Array;
        }
        if (!m_hasPsoFloat1_2D) {
            m_psoFloat1_2D = CreatePipeline(valueType, false);
            m_hasPsoFloat1_2D = true;
        }
        return m_psoFloat1_2D;

    case MipmapValueType::Float2:
        if (isArray) {
            if (!m_hasPsoFloat2_Array) {
                m_psoFloat2_Array = CreatePipeline(valueType, true);
                m_hasPsoFloat2_Array = true;
            }
            return m_psoFloat2_Array;
        }
        if (!m_hasPsoFloat2_2D) {
            m_psoFloat2_2D = CreatePipeline(valueType, false);
            m_hasPsoFloat2_2D = true;
        }
        return m_psoFloat2_2D;

    case MipmapValueType::Float4:
        if (isArray) {
            if (!m_hasPsoFloat4_Array) {
                m_psoFloat4_Array = CreatePipeline(valueType, true);
                m_hasPsoFloat4_Array = true;
            }
            return m_psoFloat4_Array;
        }
        if (!m_hasPsoFloat4_2D) {
            m_psoFloat4_2D = CreatePipeline(valueType, false);
            m_hasPsoFloat4_2D = true;
        }
        return m_psoFloat4_2D;
    }

    throw std::runtime_error("MipmappingPass: unsupported pipeline variant");
}

void TextureFactory::MipmappingPass::EnqueueJob(const std::shared_ptr<PixelBuffer>& tex, bool isSrgb) {
    if (!tex) return;

    if (tex->IsBlockCompressed()) {
        spdlog::warn("MipmappingPass: skipping block compressed texture");
        return;
    }

    const uint32_t mipLevels = tex->GetMipLevels();
    if (mipLevels <= 1) return;

    const uint32_t w = tex->GetInternalWidth();
    const uint32_t h = tex->GetInternalHeight();

    // SPD limit
    if (w > 4096 || h > 4096) {
        spdlog::warn("MipmappingPass: skipping >4K texture ({}x{}) for now", w, h);
        return;
    }

    Job j{};
    j.texture = tex;
    j.isSrgb = isSrgb;

    const uint32_t faces = tex->IsCubemap() ? 6u : 1u;
    const uint32_t slices = faces * tex->GetArraySize();
    j.sliceCount = (std::max)(1u, slices);
    j.isArray = (j.sliceCount > 1);

    if (!TryGetValueType(*tex, j.valueType)) {
        const auto& desc = tex->GetDescription();
        const rhi::Format format = desc.uavFormat != rhi::Format::Unknown
            ? desc.uavFormat
            : rhi::helpers::stripSrgb(desc.format);
        spdlog::warn("MipmappingPass: unsupported UAV format {} for GPU mip generation", static_cast<uint32_t>(format));
        return;
    }

    // SPD setup
    unsigned int workGroupOffset[2]{};
    unsigned int numWorkGroupsAndMips[2]{};
    unsigned int rectInfo[4]{ 0, 0, w, h };
    unsigned int tg[2]{};

    SpdSetup(tg, workGroupOffset, numWorkGroupsAndMips, rectInfo);

    const uint32_t maxGen = 12u;
    j.mipsToGenerate = (std::min)(mipLevels - 1u, maxGen);

    // Build constants (store CPU copy; upload happens in Update())
    MipmapSpdConstants c{};
    c.srcSize[0] = w;
    c.srcSize[1] = h;
    c.mips = j.mipsToGenerate;
    c.numWorkGroups = numWorkGroupsAndMips[0];
    c.workGroupOffset[0] = workGroupOffset[0];
    c.workGroupOffset[1] = workGroupOffset[1];
    c.invInputSize[0] = 1.0f / float((std::max)(1u, w));
    c.invInputSize[1] = 1.0f / float((std::max)(1u, h));
    c.flags = isSrgb ? 1u : 0u;
    c.srcMip = 0;

    for (uint32_t i = 0; i < 12u; ++i) {
        c.mipUavDescriptorIndices[i] = 0;
    }

    // Fill mip1..mipN UAV indices
    for (uint32_t i = 0; i < j.mipsToGenerate; ++i) {
        c.mipUavDescriptorIndices[i] = tex->GetUAVShaderVisibleInfo(i + 1).slot.index;
    }

    j.dispatchThreadGroupCountXY[0] = tg[0];
    j.dispatchThreadGroupCountXY[1] = tg[1];

    // Allocate a constants view
    j.constantsView = m_pMipConstants->Add();
    j.constantsIndex = static_cast<uint32_t>(j.constantsView->GetOffset() / sizeof(MipmapSpdConstants));
    j.cpuConstants = c;

    m_pMipConstants->UpdateView(j.constantsView.get(), &j.cpuConstants);

    // Per-job counter buffer: RWStructuredBuffer<uint> with elementCount = sliceCount
    j.counter = CreateIndexedStructuredBuffer(
        /*numElements=*/ j.sliceCount,
        /*stride=*/ sizeof(uint32_t),
        /*uav=*/ true);

    m_declaredResourcesChanged = true;
    m_pending.push_back(std::move(j));
}


void TextureFactory::MipmappingPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    if (m_pending.empty()) return;

    builder->WithShaderResource(m_pMipConstants);

    for (auto& j : m_pending) {
        auto tex = j.texture;
        if (!tex) continue;

        // Read mip0, write mip1..N
        builder->WithShaderResource(Subresources(tex, Mip{ 0, 1 }));
        if (j.mipsToGenerate > 0) {
            builder->WithUnorderedAccess(Subresources(tex, FromMip{ 1 }));
        }

        // Counter is UAV for the dispatch
        builder->WithUnorderedAccess(j.counter);
    }
}

PassReturn TextureFactory::MipmappingPass::Execute(PassExecutionContext& executionContext)
{
    if (m_pending.empty()) return {};

    auto& psoManager = PSOManager::GetInstance();
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

    commandList.BindLayout(psoManager.GetComputeRootSignature().GetHandle());

    const uint32_t constantsSrvIndex = m_pMipConstants->GetSRVInfo(0).slot.index;

    // Process all jobs queued for this frame
    for (auto& j : m_pending) {
        if (!j.texture) continue;

        // Pick SRV (2D vs array)
        const uint32_t srcSrvIndex =
            j.isArray
            ? j.texture->GetSRVInfo(SRVViewType::Texture2DArray, 0).slot.index
            : j.texture->GetSRVInfo(0).slot.index;

        PipelineState& pso = GetOrCreatePipeline(j.valueType, j.isArray);

        commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

        unsigned int root[NumMiscUintRootConstants]{};
        root[UintRootConstant0] = j.counter->GetUAVShaderVisibleInfo(0).slot.index;
        root[UintRootConstant1] = srcSrvIndex;
        root[UintRootConstant2] = constantsSrvIndex;
        root[UintRootConstant3] = j.constantsIndex;

        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            root);

        commandList.Dispatch(
            j.dispatchThreadGroupCountXY[0],
            j.dispatchThreadGroupCountXY[1],
            j.sliceCount);
    }

    // Clear jobs
    m_declaredResourcesChanged = true;
    m_pending.clear();

    return {};
}

void TextureFactory::BC7CompressionPass::Setup()
{
}

void TextureFactory::BC7CompressionPass::EnqueueJob(const std::shared_ptr<BC7CompressionJob>& job)
{
    if (!job) {
        return;
    }

    m_pending.push_back(job);
    m_declaredResourcesChanged = true;
}

void TextureFactory::BC7CompressionPass::Update(const UpdateExecutionContext& context)
{
    (void)context;
}

PipelineState TextureFactory::BC7CompressionPass::CreatePipeline() const
{
    auto& psoManager = PSOManager::GetInstance();
    return psoManager.MakeComputePipeline(
        psoManager.GetComputeRootSignature().GetHandle(),
        L"shaders/Utilities/bc7_compress_mode6.hlsl",
        L"BC7CompressMode6CS",
        {},
        "BC7Compression[Mode6]");
}

PipelineState& TextureFactory::BC7CompressionPass::GetOrCreatePipeline()
{
    if (!m_hasPsoMode6) {
        m_psoMode6 = CreatePipeline();
        m_hasPsoMode6 = true;
    }

    return m_psoMode6;
}

void TextureFactory::BC7CompressionPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    if (m_pending.empty()) {
        return;
    }

    for (const auto& job : m_pending) {
        if (!job || !job->workingTexture || !job->blockBuffer) {
            continue;
        }

        for (const auto& subresource : job->subresources) {
            builder->WithShaderResource(Subresources(job->workingTexture, Mip{subresource.mip, 1}, Slice{subresource.slice, 1}));
        }
        builder->WithUnorderedAccess(job->blockBuffer);
    }
}

PassReturn TextureFactory::BC7CompressionPass::Execute(PassExecutionContext& executionContext)
{
    if (m_pending.empty()) {
        return {};
    }

    auto& psoManager = PSOManager::GetInstance();
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    if (!renderContext) {
        return {};
    }

    auto& commandList = executionContext.commandList;
    commandList.SetDescriptorHeaps(renderContext->textureDescriptorHeap.GetHandle(), renderContext->samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(psoManager.GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(GetOrCreatePipeline().GetAPIPipelineState().GetHandle());

    for (const auto& job : m_pending) {
        if (!job || !job->workingTexture || !job->blockBuffer) {
            continue;
        }

        for (const auto& subresource : job->subresources) {
            unsigned int root[NumMiscUintRootConstants]{};
            root[UintRootConstant0] = job->workingTexture->GetSRVInfo(subresource.mip, subresource.slice).slot.index;
            root[UintRootConstant1] = job->blockBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            root[UintRootConstant2] = static_cast<uint32_t>(subresource.footprint.offset);
            root[UintRootConstant3] = subresource.footprint.rowPitch;
            root[UintRootConstant4] = subresource.footprint.width;
            root[UintRootConstant5] = subresource.footprint.height;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                root);

            const uint32_t blocksX = (subresource.footprint.width + 3u) / 4u;
            const uint32_t blocksY = (subresource.footprint.height + 3u) / 4u;
            const uint32_t dispatchX = (blocksX + 7u) / 8u;
            const uint32_t dispatchY = (blocksY + 7u) / 8u;
            commandList.Dispatch(dispatchX, dispatchY, 1);
        }
    }

    m_pending.clear();
    m_declaredResourcesChanged = true;
    return {};
}

void TextureFactory::BC7CompressionPass::Cleanup()
{
}

void TextureFactory::BC7CompressionCopyPass::Setup()
{
}

void TextureFactory::BC7CompressionCopyPass::EnqueueJob(const std::shared_ptr<BC7CompressionJob>& job)
{
    if (!job) {
        return;
    }

    m_pending.push_back(job);
    m_declaredResourcesChanged = true;
}

void TextureFactory::BC7CompressionCopyPass::Update(const UpdateExecutionContext& context)
{
    (void)context;
}

void TextureFactory::BC7CompressionCopyPass::DeclareResourceUsages(CopyPassBuilder* builder)
{
    if (m_pending.empty()) {
        return;
    }

    builder->PreferQueue(QueueKind::Copy);
    for (const auto& job : m_pending) {
        if (!job || !job->blockBuffer || !job->compressedTexture) {
            continue;
        }

        builder->WithCopySource(job->blockBuffer);
        builder->WithCopyDest(job->compressedTexture);
    }
}

void TextureFactory::BC7CompressionCopyPass::RecordImmediateCommands(ImmediateExecutionContext& context)
{
    for (const auto& job : m_pending) {
        if (!job || !job->blockBuffer || !job->compressedTexture) {
            continue;
        }

        for (const auto& subresource : job->subresources) {
            context.list.CopyBufferToTexture(
                job->blockBuffer,
                job->compressedTexture,
                subresource.mip,
                subresource.slice,
                subresource.footprint,
                0,
                0,
                0);
        }
    }

    m_pending.clear();
    m_declaredResourcesChanged = true;
}

void TextureFactory::BC7CompressionCopyPass::Cleanup()
{
}

void TextureFactory::BC7CompressionReadbackPass::Setup()
{
}

void TextureFactory::BC7CompressionReadbackPass::SetReadbackService(rg::runtime::IReadbackService* readbackService)
{
    m_readbackService = readbackService;
}

void TextureFactory::BC7CompressionReadbackPass::EnqueueJob(const std::shared_ptr<BC7CompressionJob>& job)
{
    if (!job) {
        return;
    }

    m_pending.push_back(job);
    m_declaredResourcesChanged = true;
}

void TextureFactory::BC7CompressionReadbackPass::Update(const UpdateExecutionContext& context)
{
    (void)context;
}

void TextureFactory::BC7CompressionReadbackPass::DeclareResourceUsages(CopyPassBuilder* builder)
{
    if (m_pending.empty()) {
        return;
    }

    builder->PreferQueue(QueueKind::Copy);
    for (const auto& job : m_pending) {
        if (!job || !job->compressedTexture) {
            continue;
        }

        builder->WithCopySource(job->compressedTexture);
    }
}

void TextureFactory::BC7CompressionReadbackPass::RecordImmediateCommands(ImmediateExecutionContext& context)
{
    if (!m_readbackService) {
        return;
    }

    for (const auto& job : m_pending) {
        if (!job || !job->compressedTexture || !job->handle) {
            continue;
        }

        std::vector<rhi::CopyableFootprint> footprints(job->subresources.size());
        rhi::FootprintRangeDesc footprintRange{};
        footprintRange.texture = job->compressedTexture->GetAPIResource().GetHandle();
        footprintRange.firstMip = 0;
        footprintRange.mipCount = static_cast<uint32_t>(job->subresources.size());
        footprintRange.firstArraySlice = 0;
        footprintRange.arraySize = 1;
        footprintRange.firstPlane = 0;
        footprintRange.planeCount = 1;
        footprintRange.baseOffset = 0;

        auto footprintInfo = context.device.GetCopyableFootprints(
            footprintRange,
            footprints.data(),
            static_cast<uint32_t>(footprints.size()));

        auto readbackBuffer = Buffer::CreateShared(rhi::HeapType::Readback, footprintInfo.totalBytes);
        if (!job->debugName.empty()) {
            readbackBuffer->SetName(job->debugName + "[BC7Readback]");
        }

        for (size_t index = 0; index < job->subresources.size(); ++index) {
            const auto& subresource = job->subresources[index];
            context.list.CopyTextureToBuffer(
                job->compressedTexture.get(),
                subresource.mip,
                subresource.slice,
                readbackBuffer.get(),
                footprints[index],
                0,
                0,
                0);
        }

        ReadbackCaptureRequest request{};
        request.desc.kind = ReadbackResourceKind::Texture;
        request.desc.resourceId = job->compressedTexture->GetGlobalResourceID();
        request.readbackBuffer = readbackBuffer;
        request.layouts = footprints;
        request.totalSize = footprintInfo.totalBytes;
        request.format = job->compressedTexture->GetFormat();
        request.width = job->compressedTexture->GetWidth();
        request.height = job->compressedTexture->GetHeight();
        request.depth = 1;
        request.callback = [job](ReadbackCaptureResult&& readback) {
            try {
                auto result = BuildCompressedSourceDataFromReadback(
                    job->compressedTexture->GetDescription(),
                    true,
                    readback);
                TextureProcessingManager::GetInstance().CompleteGpuProcessing(
                    job->handle,
                    std::move(result),
                    job->compressedTexture,
                    true);
            }
            catch (const std::exception& ex) {
                TextureProcessingManager::GetInstance().FailProcessing(job->handle, ex.what());
            }
        };

        const auto token = m_readbackService->EnqueueCapture(std::move(request));
        m_pendingCaptureIds.push_back(token.id);
        TextureProcessingManager::GetInstance().MarkGpuJobReadbackPending(job->handle);
    }

    m_pending.clear();
    m_declaredResourcesChanged = true;
}

PassReturn TextureFactory::BC7CompressionReadbackPass::Execute(PassExecutionContext& context)
{
    if (!m_readbackService || m_pendingCaptureIds.empty()) {
        return {};
    }

    const uint64_t fenceValue = m_readbackService->GetNextReadbackFenceValue();
    for (uint64_t captureId : m_pendingCaptureIds) {
        m_readbackService->FinalizeCapture(rg::runtime::ReadbackCaptureToken{ captureId }, fenceValue);
    }

    m_pendingCaptureIds.clear();
    return { m_readbackService->GetReadbackFence(), fenceValue };
}

void TextureFactory::BC7CompressionReadbackPass::Cleanup()
{
}