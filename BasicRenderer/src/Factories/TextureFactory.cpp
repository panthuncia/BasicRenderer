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

void TextureFactory::MipmappingPass::EnqueueJob(const std::shared_ptr<PixelBuffer>& tex, bool isSrgb) {
    if (!tex) return;

    if (tex->IsBlockCompressed()) {
	    spdlog::warn("MipmappingPass: skipping block compressed texture");
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

    // Decide scalar vs vector (keep it simple: 1-channel => scalar, else vector)
    j.isScalar = (tex->GetChannelCount() == 1);

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

    m_pending.push_back(std::move(j));
}


void TextureFactory::MipmappingPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    if (m_pending.empty()) return;

    builder->WithShaderResource(m_pMipConstants);

    // Exit state: shader-readable after we internally transition outputs back
    const ResourceState exitSRV{
        rhi::ResourceAccessType::ShaderResource,
        AccessToLayout(rhi::ResourceAccessType::ShaderResource, /*isRender=*/false),
        rhi::ResourceSyncState::ComputeShading
    };

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

PassReturn TextureFactory::MipmappingPass::Execute(RenderContext& context)
{
    if (m_pending.empty()) return {};

    auto& psoManager = PSOManager::GetInstance();
    auto& commandList = context.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

    commandList.BindLayout(psoManager.GetRootSignature().GetHandle());

    const uint32_t constantsSrvIndex = m_pMipConstants->GetSRVInfo(0).slot.index;

    // Process all jobs queued for this frame
    for (auto& j : m_pending) {
        if (!j.texture) continue;

        // Pick SRV (2D vs array)
        const uint32_t srcSrvIndex =
            j.isArray
            ? j.texture->GetSRVInfo(SRVViewType::Texture2DArray, 0).slot.index
            : j.texture->GetSRVInfo(0).slot.index;

        // Pick pipeline
        PipelineState* pso = nullptr;
        if (j.isScalar) {
            pso = j.isArray ? &m_psoScalarArray : &m_psoScalar2D;
        }
        else {
            pso = j.isArray ? &m_psoVecArray : &m_psoVec2D;
        }

        commandList.BindPipeline(pso->GetAPIPipelineState().GetHandle());

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
	m_pending.clear();

    return {};
}