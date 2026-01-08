#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string_view>

#include "Resources/TextureDescription.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "RenderPasses/Base/ComputePass.h"

class PixelBuffer;
class Sampler;
class BufferView;

// Central API for textures that have initial texel data.
class TextureFactory {
public:
    static std::unique_ptr<TextureFactory> CreateUnique() {
		return std::unique_ptr<TextureFactory>(new TextureFactory());
    }
    // Owned initial texel bytes for a texture creation request.
	// Subresource order is: [slice0 mip0..mipN-1, slice1 ...].
    struct TextureInitialData {
        std::vector<std::shared_ptr<std::vector<uint8_t>>> subresources;

        bool Empty() const noexcept { return subresources.empty(); }

        static TextureInitialData FromBytes(const std::vector<std::shared_ptr<std::vector<uint8_t>>>& bytes) {
            TextureInitialData d;
            d.subresources = bytes;
            return d;
        }
    };

    std::shared_ptr<PixelBuffer> CreateAlwaysResidentPixelBuffer(
        TextureDescription desc,
        TextureInitialData initialData,
        std::string_view debugName = {}) const;

private:

    class MipmappingPass : public ComputePass {
    public:
        MipmappingPass();

        void Enqueue(const std::shared_ptr<PixelBuffer>& tex, const TextureDescription& desc);

        void Setup() override {}
        void Update() override;
        void Cleanup(RenderContext&) override {
			// Destroy kept-alive resources
			m_liveConstantsViews.clear();
			m_liveTextures.clear();
			m_liveCounters.clear();
            m_pMipConstants.reset();
            m_jobs.clear();
        }
        PassReturn Execute(RenderContext& context) override;

    private:
        struct MipmapSpdConstants
        {
            uint32_t srcSize[2];
            uint32_t mips;
            uint32_t numWorkGroups;

            uint32_t workGroupOffset[2];
            float    invInputSize[2];

            uint32_t srcMip;
            uint32_t flags;

            uint32_t mipUavDescriptorIndices[11];
            uint32_t pad[3];
        };

        struct Chunk
        {
            uint32_t srcMip = 0; // source mip for this chunk
            uint32_t generated = 0; // <= 11
            uint32_t dispatchXY[2]{};

            MipmapSpdConstants constantsCPU{};
            std::shared_ptr<BufferView> constantsView;
            uint32_t constantsIndex = 0;
            bool constantsUploaded = false;

            std::shared_ptr<GloballyIndexedResource> counter; // unique per dispatch
        };

        struct Job
        {
            std::shared_ptr<PixelBuffer> tex;
            TextureDescription desc{};

            uint32_t arraySlices = 1;
            uint32_t textureSrvIndex = 0; // one SRV for whole texture
            PipelineState* pso = nullptr;

            std::vector<Chunk> chunks;
        };

        std::vector<Job> m_jobs;

        // Keep alive
        std::vector<std::shared_ptr<BufferView>> m_liveConstantsViews;
        std::vector<std::shared_ptr<PixelBuffer>> m_liveTextures;
        std::vector<std::shared_ptr<GloballyIndexedResource>> m_liveCounters;

        std::shared_ptr<LazyDynamicStructuredBuffer<MipmapSpdConstants>> m_pMipConstants;

        PipelineState m_mipRGBA2D;
        PipelineState m_mipRGBAArray;
        PipelineState m_mipScalar2D;
        PipelineState m_mipScalarArray;

    private:
        void CreatePipelines();
        void EmitTextureBarrier_UAVtoUAV(
            rhi::CommandList& commandList,
            rhi::ResourceHandle texHandle,
            uint32_t baseMip, uint32_t mipCount,
            uint32_t baseSlice, uint32_t sliceCount);
    };

    TextureFactory() {
		m_mipmappingPass = std::make_shared<MipmappingPass>();
    }

	std::shared_ptr<ComputePass> m_mipmappingPass;
};
