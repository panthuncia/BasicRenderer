#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string_view>

#include "Resources/TextureDescription.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/PassBuilders.h"

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
        MipmappingPass()
        {
            m_pMipConstants = LazyDynamicStructuredBuffer<MipmapSpdConstants>::CreateShared(
                64, "Mipmap SPD constants");

            CreatePipelines();
        }

        //bool WantsPerFrameResourceDeclarations() const noexcept override { return true; }

        void Setup() override {
	        
        }

        // Called by TextureFactory when you create a texture with only mip0 uploaded.
        void EnqueueJob(const std::shared_ptr<PixelBuffer>& tex, bool isSrgb);

        void DeclareResourceUsages(ComputePassBuilder* builder) override;

        void Update(const UpdateContext& context) override
        {
            // retire resources from N frames ago
            RetireOldJobs(context.frameIndex);

            for (auto& j : m_pending) {
                if (j.constantsDirty && j.constantsView) {
                    m_pMipConstants->UpdateView(j.constantsView.get(), &j.cpuConstants);
                    j.constantsDirty = false;
                }
            }
        }

        PassReturn Execute(RenderContext& context) override;

        void Cleanup() override {}

    private:
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
            bool isScalar = false;
            bool isSrgb = false;
            bool constantsDirty = false;
        };

        std::vector<Job> m_pending;

        // simple ring retire (adapt count to your frames-in-flight)
        static constexpr uint32_t kFramesInFlight = 3;
        std::vector<Job> m_retire[kFramesInFlight];

        std::shared_ptr<LazyDynamicStructuredBuffer<MipmapSpdConstants>> m_pMipConstants;

        PipelineState m_psoVec2D;
        PipelineState m_psoVecArray;
        PipelineState m_psoScalar2D;
        PipelineState m_psoScalarArray;

        void CreatePipelines()
        {
            auto& psoManager = PSOManager::GetInstance();
            auto& layout = psoManager.GetRootSignature();

            m_psoVec2D = psoManager.MakeComputePipeline(
                layout,
                L"shaders/Utilities/mipmapping.hlsl",
                L"MipmapCSMain",
                { }, // no defines
                "MipmapSPD[Vec2D]");

            m_psoVecArray = psoManager.MakeComputePipeline(
                layout,
                L"shaders/Utilities/mipmapping.hlsl",
                L"MipmapCSMain",
                { DxcDefine{ L"MIPMAP_ARRAY", L"1" } },
                "MipmapSPD[VecArray]");

            m_psoScalar2D = psoManager.MakeComputePipeline(
                layout,
                L"shaders/Utilities/mipmapping.hlsl",
                L"MipmapCSMain",
                { DxcDefine{ L"MIPMAP_SCALAR", L"1" } },
                "MipmapSPD[Scalar2D]");

            m_psoScalarArray = psoManager.MakeComputePipeline(
                layout,
                L"shaders/Utilities/mipmapping.hlsl",
                L"MipmapCSMain",
                { DxcDefine{ L"MIPMAP_SCALAR", L"1" }, DxcDefine{ L"MIPMAP_ARRAY", L"1" } },
                "MipmapSPD[ScalarArray]");
        }

        void StashCompletedJobs(uint8_t frameIndex)
        {
            auto& slot = m_retire[frameIndex % kFramesInFlight];
            slot.insert(slot.end(),
                std::make_move_iterator(m_pending.begin()),
                std::make_move_iterator(m_pending.end()));
            m_pending.clear();
        }

        void RetireOldJobs(uint8_t frameIndex)
        {
            auto& slot = m_retire[frameIndex % kFramesInFlight];

            // Remove constants views now that GPU should be done with this frame slot.
            for (auto& j : slot) {
                if (j.constantsView) {
                    m_pMipConstants->Remove(j.constantsView.get());
                }
            }
            slot.clear();
        }
    };

    TextureFactory() {
		m_mipmappingPass = std::make_shared<MipmappingPass>();
    }

	std::shared_ptr<ComputePass> m_mipmappingPass;
};
