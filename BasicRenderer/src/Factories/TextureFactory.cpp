#include "Factories/TextureFactory.h"

#include <algorithm>
#include <stdexcept>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "Resources/PixelBuffer.h"
#include "Resources/Sampler.h"
#include "ThirdParty/stb/stb_image.h"
#include "rhi_helpers.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Managers/Singletons/PSOManager.h"

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
            UploadManager::UploadTarget::FromShared(dstTexture),
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
            // Interpret “color” channels as sRGB if isSRGB, otherwise treat as linear UNORM.
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

    // if caller asked for mipmaps but only provided mip0 for a single-slice 2D texture, generate full chain on CPU.
	if (desc.generateMipMaps && !rhi::helpers::IsBlockCompressed(desc.format)) { // TODO: BC mip gen
        const bool singleSlice = (!desc.isArray && !desc.isCubemap && desc.arraySize == 1);
        if (singleSlice && initialData.subresources.size() == 1) {
            const uint32_t mipLevels = CalcMipCount(baseW, baseH);
            initialData.subresources = BuildMipChain2D(initialData.subresources[0], baseW, baseH, desc.channels, mipLevels, rhi::helpers::IsSRGB(desc.format), false);

            // Set dimensions for all mips
            desc.imageDimensions.resize(mipLevels);
            //if (desc.imageDimensions[0].rowPitch == 0) {
            //    desc.imageDimensions[0].rowPitch = static_cast<uint64_t>(baseW) * desc.channels;
            //}
            //if (desc.imageDimensions[0].slicePitch == 0) {
            //    desc.imageDimensions[0].slicePitch = desc.imageDimensions[0].rowPitch * baseH;
            //}

            for (uint32_t m = 0; m < mipLevels; ++m) {
                const uint32_t w = std::max(1u, baseW >> m);
                const uint32_t h = std::max(1u, baseH >> m);
                desc.imageDimensions[m].width = w;
                desc.imageDimensions[m].height = h;
                desc.imageDimensions[m].rowPitch = static_cast<uint64_t>(w) * desc.channels;
                desc.imageDimensions[m].slicePitch = desc.imageDimensions[m].rowPitch * h;
            }
        }
    }

    auto pb = PixelBuffer::CreateShared(desc);

    if (!debugName.empty()) {
        pb->SetName(std::string(debugName));
    }

    UploadTextureData(pb, desc, initialData.subresources, pb->GetMipLevels());

    return pb;
}

TextureFactory::MipmappingPass::MipmappingPass()
{
    m_pMipConstants = LazyDynamicStructuredBuffer<MipmapSpdConstants>::CreateShared(1, "Mipmapping SPD constants");

    CreatePipelines();
}

void TextureFactory::MipmappingPass::CreatePipelines()
{
    auto& psoManager = PSOManager::GetInstance();
    auto& layout = psoManager.GetRootSignature();

    m_mipRGBA2D = psoManager.MakeComputePipeline(
        layout,
        L"shaders/Utilities/mipmapping.hlsl",
        L"MipmapCSMain",
        {}, // no defines
        "MipmappingCS[RGBA2D]"
    );

    m_mipRGBAArray = psoManager.MakeComputePipeline(
        layout,
        L"shaders/Utilities/mipmapping.hlsl",
        L"MipmapCSMain",
        {DxcDefine{ L"MIPMAP_ARRAY", L"1" }},
        "MipmappingCS[RGBAArray]"
    );

    m_mipScalar2D = psoManager.MakeComputePipeline(
        layout,
        L"shaders/Utilities/mipmapping.hlsl",
        L"MipmapCSMain",
        {DxcDefine{ L"MIPMAP_SCALAR", L"1" }},
        "MipmappingCS[Scalar2D]"
    );

    m_mipScalarArray = psoManager.MakeComputePipeline(
        layout,
        L"shaders/Utilities/mipmapping.hlsl",
        L"MipmapCSMain",
        { DxcDefine{L"MIPMAP_ARRAY", L"1"} , DxcDefine{L"MIPMAP_SCALAR", L"1"} },
        "MipmappingCS[ScalarArray]"
    );
}

void TextureFactory::MipmappingPass::Enqueue(const std::shared_ptr<PixelBuffer>& tex, const TextureDescription& desc)
{
    if (!tex) return;

    const uint32_t totalMipLevels = tex->GetMipLevels();
    if (totalMipLevels <= 1) return;
    if (rhi::helpers::IsBlockCompressed(desc.format)) return;

    const uint32_t faces = desc.isCubemap ? 6u : 1u;
    const uint32_t arraySlices = faces * static_cast<uint32_t>(desc.arraySize);
    if (arraySlices == 0) return;

    Job job{};
    job.tex = tex;
    job.desc = desc;
    job.arraySlices = arraySlices;

    const bool isArrayLike = desc.isCubemap || desc.isArray || (desc.arraySize > 1);
    const bool isScalar = (desc.channels == 1);

    job.pso = isScalar
        ? (isArrayLike ? &m_mipScalarArray : &m_mipScalar2D)
        : (isArrayLike ? &m_mipRGBAArray : &m_mipRGBA2D);

    // SRV must include the full mip chain so shader can Load() arbitrary mip.
    job.textureSrvIndex =
        isArrayLike
        ? tex->GetSRVInfo(SRVViewType::Texture2DArray, 0).slot.index
        : tex->GetSRVInfo(0).slot.index;

    const uint32_t baseW = tex->GetInternalWidth();
    const uint32_t baseH = tex->GetInternalHeight();

    uint32_t remaining = totalMipLevels - 1; // number of mips to generate beyond mip0
    uint32_t srcMip = 0;

    while (remaining > 0)
    {
        Chunk chunk{};
        chunk.srcMip = srcMip;

        chunk.generated = (std::min)(remaining, 11u);

        // source mip size
        const uint32_t srcW = (std::max)(1u, baseW >> srcMip);
        const uint32_t srcH = (std::max)(1u, baseH >> srcMip);

        // SPD CPU setup for this source mip size
        unsigned int workGroupOffset[2] = {};
        unsigned int numWorkGroupsAndMips[2] = {};
        unsigned int rectInfo[4] = { 0u, 0u, srcW, srcH };
        unsigned int dispatchThreadGroupCountXY[2] = {};

        SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

        chunk.dispatchXY[0] = dispatchThreadGroupCountXY[0];
        chunk.dispatchXY[1] = dispatchThreadGroupCountXY[1];

        // fill constants
        auto& c = chunk.constantsCPU;
        c.srcSize[0] = srcW;
        c.srcSize[1] = srcH;
        c.invInputSize[0] = (srcW > 0) ? (1.0f / float(srcW)) : 0.0f;
        c.invInputSize[1] = (srcH > 0) ? (1.0f / float(srcH)) : 0.0f;

        c.mips = chunk.generated;
        c.numWorkGroups = numWorkGroupsAndMips[0];
        c.workGroupOffset[0] = workGroupOffset[0];
        c.workGroupOffset[1] = workGroupOffset[1];

        c.srcMip = srcMip;
        c.flags = rhi::helpers::IsSRGB(desc.format) ? 1u : 0u;

        // Destination mips are absolute: srcMip+1 .. srcMip+generated
        for (uint32_t i = 0; i < chunk.generated; ++i)
        {
            const uint32_t dstMip = srcMip + 1 + i;
            c.mipUavDescriptorIndices[i] = tex->GetUAVShaderVisibleInfo(dstMip).slot.index;
        }

        // allocate constants slot now; upload later in Update()
        chunk.constantsView = m_pMipConstants->Add();
        chunk.constantsIndex = static_cast<uint32_t>(chunk.constantsView->GetOffset() / sizeof(MipmapSpdConstants));

        // allocate a dedicated counter buffer
        chunk.counter = CreateIndexedStructuredBuffer(1, sizeof(unsigned int) * 6u * 6u, true);

        job.chunks.push_back(std::move(chunk));

        // advance
        remaining -= chunk.generated;
        srcMip += chunk.generated;
    }

    m_jobs.push_back(std::move(job));
}

void TextureFactory::MipmappingPass::Update()
{
    for (auto& job : m_jobs)
    {
        for (auto& chunk : job.chunks)
        {
            if (chunk.constantsUploaded) continue;
            if (!chunk.constantsView) continue;

            m_pMipConstants->UpdateView(chunk.constantsView.get(), &chunk.constantsCPU);
            chunk.constantsUploaded = true;

            // keep alive (safe)
            m_liveConstantsViews.push_back(chunk.constantsView);
            m_liveCounters.push_back(chunk.counter);
        }

        m_liveTextures.push_back(job.tex);
    }
}

void TextureFactory::MipmappingPass::EmitTextureBarrier_UAVtoUAV(
    rhi::CommandList& commandList,
    rhi::ResourceHandle texHandle,
    uint32_t baseMip, uint32_t mipCount,
    uint32_t baseSlice, uint32_t sliceCount)
{
    rhi::TextureBarrier barrier{};
    barrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    barrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
    barrier.afterSync = rhi::ResourceSyncState::ComputeShading;

    barrier.beforeLayout = rhi::ResourceLayout::UnorderedAccess;
    barrier.afterLayout = rhi::ResourceLayout::UnorderedAccess;

    barrier.texture = texHandle;

    rhi::TextureSubresourceRange range;
    range.baseMip = baseMip;
    range.mipCount = mipCount;
    range.baseLayer = baseSlice;
    range.layerCount = sliceCount;
	barrier.range = range;

    rhi::BarrierBatch batch{};
    batch.textures = rhi::Span<rhi::TextureBarrier>(&barrier, 1);
    commandList.Barriers(batch);
}

PassReturn TextureFactory::MipmappingPass::Execute(RenderContext& context)
{
    if (m_jobs.empty()) return {};

    auto& psoManager = PSOManager::GetInstance();
    auto& commandList = context.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(),
        context.samplerDescriptorHeap.GetHandle());

    commandList.BindLayout(psoManager.GetRootSignature().GetHandle());

    const uint32_t constantsSrvIndex = m_pMipConstants->GetSRVInfo(0).slot.index;

    for (auto& job : m_jobs)
    {
        if (!job.tex || !job.pso) continue;

        commandList.BindPipeline(job.pso->GetAPIPipelineState().GetHandle());

        const auto texHandle = job.tex->GetAPIResource().GetHandle();

        for (size_t i = 0; i < job.chunks.size(); ++i)
        {
            auto& chunk = job.chunks[i];
            if (!chunk.constantsUploaded) continue;

            const uint32_t counterUavIndex = chunk.counter->GetUAVShaderVisibleInfo(0).slot.index;

            unsigned int root[NumMiscUintRootConstants] = {};
            root[UintRootConstant0] = counterUavIndex;
            root[UintRootConstant1] = job.textureSrvIndex;   // SRV for whole texture
            root[UintRootConstant2] = constantsSrvIndex;
            root[UintRootConstant3] = chunk.constantsIndex;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                root
            );

            commandList.Dispatch(chunk.dispatchXY[0], chunk.dispatchXY[1], job.arraySlices);

            // If there is another chunk, the next chunk reads from the last generated mip:
            // nextSrcMip = srcMip + generated (that mip was just written as UAV).
            if (i + 1 < job.chunks.size())
            {
                const uint32_t nextSrcMip = chunk.srcMip + chunk.generated;

                // Make nextSrcMip readable for the next dispatch
                EmitTextureBarrier_UAVtoUAV(commandList, texHandle,
                    /*baseMip*/ nextSrcMip, /*mipCount*/ 1,
                    /*baseSlice*/ 0, /*sliceCount*/ job.arraySlices);

                // Also make sure the next chunk's destination range is synced
                const uint32_t nextDstBase = nextSrcMip + 1;
                const uint32_t nextDstCount = job.chunks[i + 1].generated;
                EmitTextureBarrier_UAVtoUAV(commandList, texHandle,
                    nextDstBase, nextDstCount,
                    0, job.arraySlices);
            }
        }
    }

    m_jobs.clear();
    return {};
}
