#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string_view>

#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Managers/Singletons/PSOManager.h"
#include "OpenRenderGraph/OpenRenderGraph.h"

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

    std::shared_ptr<ComputePass> GetMipmappingPass() const { return m_mipmappingPass; }

private:

    class MipmappingPass : public ComputePass, public IDynamicDeclaredResources {
    public:
        MipmappingPass()
        {
            m_pMipConstants = LazyDynamicStructuredBuffer<MipmapSpdConstants>::CreateShared(
                64, "Mipmap SPD constants");
        }

        void Setup() override {
	        
        }

        // Called by TextureFactory when you create a texture with only mip0 uploaded.
        void EnqueueJob(const std::shared_ptr<PixelBuffer>& tex, bool isSrgb);

        void DeclareResourceUsages(ComputePassBuilder* builder) override;

        void Update(const UpdateExecutionContext& context) override {}

        PassReturn Execute(PassExecutionContext& context) override;

        void Cleanup() override {
        }

        bool DeclaredResourcesChanged() const override {
            return m_declaredResourcesChanged;
        }

    private:
        enum class MipmapValueType
        {
            Float1,
            Float2,
            Float4,
        };

        struct MipmapSpdConstants
        {
            uint32_t srcSize[2];
            uint32_t mips;
            uint32_t numWorkGroups;

            uint32_t workGroupOffset[2];
            float    invInputSize[2];

            uint32_t mipUavDescriptorIndices[12];
            uint32_t flags;
            uint32_t srcMip;
            uint32_t pad0;
            uint32_t pad1;
        };

        struct Job
        {
            std::shared_ptr<PixelBuffer> texture;
            std::shared_ptr<BufferView>  constantsView;
            std::shared_ptr<GloballyIndexedResource> counter;

            MipmapSpdConstants cpuConstants{};
            uint32_t constantsIndex = 0;

            uint32_t dispatchThreadGroupCountXY[2]{};
            uint32_t sliceCount = 1;
            uint32_t mipsToGenerate = 0;

            bool isArray = false;
            bool isSrgb = false;
            MipmapValueType valueType = MipmapValueType::Float4;
        };

        std::vector<Job> m_pending;

        std::shared_ptr<LazyDynamicStructuredBuffer<MipmapSpdConstants>> m_pMipConstants;

        PipelineState m_psoFloat1_2D;
        PipelineState m_psoFloat1_Array;
        PipelineState m_psoFloat2_2D;
        PipelineState m_psoFloat2_Array;
        PipelineState m_psoFloat4_2D;
        PipelineState m_psoFloat4_Array;

        bool m_hasPsoFloat1_2D = false;
        bool m_hasPsoFloat1_Array = false;
        bool m_hasPsoFloat2_2D = false;
        bool m_hasPsoFloat2_Array = false;
        bool m_hasPsoFloat4_2D = false;
        bool m_hasPsoFloat4_Array = false;

        static bool TryGetValueType(const PixelBuffer& tex, MipmapValueType& outValueType);
        PipelineState& GetOrCreatePipeline(MipmapValueType valueType, bool isArray);
        PipelineState CreatePipeline(MipmapValueType valueType, bool isArray) const;

        bool m_declaredResourcesChanged = true;
    };

    TextureFactory() {
		m_mipmappingPass = std::make_shared<MipmappingPass>();
    }

	std::shared_ptr<ComputePass> m_mipmappingPass;
};
