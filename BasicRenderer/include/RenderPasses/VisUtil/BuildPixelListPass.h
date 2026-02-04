#pragma once
#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "../shaders/PerPassRootConstants/visUtilRootConstants.h"

class BuildPixelListPass : public ComputePass {
public:
    BuildPixelListPass() {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"shaders/VisUtil.hlsl",
            L"BuildPixelListCS",
            {},
            "BuildPixelListPSO");
	}
    void DeclareResourceUsages(ComputePassBuilder* b) override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();

		// Global LOD extension visibility buffer tag
        auto visBufferTag = ecsWorld.component<CLodExtensionVisibilityBufferTag>();

		// Query for entities with the visibility buffer tag
        m_visibleClustersQuery =
            ecsWorld.query_builder<flecs::entity>()
            .with<CLodExtensionTypeTag>(visBufferTag)
			.with<VisibleClustersBufferTag>()
            .build();

        b->WithShaderResource(ECSResourceResolver(m_visibleClustersQuery));

        b->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS,
                              Builtin::PrimaryCamera::VisibilityTexture,
                              //Builtin::PrimaryCamera::VisibleClusterTable,
                              Builtin::PerMeshInstanceBuffer,
                              Builtin::PerMeshBuffer,
                              Builtin::PerMaterialDataBuffer,
                              "Builtin::VisUtil::MaterialOffsetBuffer")
         .WithUnorderedAccess("Builtin::VisUtil::MaterialWriteCursorBuffer",
                              "Builtin::VisUtil::PixelListBuffer");
    }

    void Setup() override {
        RegisterSRV(Builtin::PrimaryCamera::VisibilityTexture);
        //RegisterSRV(Builtin::PrimaryCamera::VisibleClusterTable);
        RegisterSRV(Builtin::PerMeshInstanceBuffer);
        RegisterSRV(Builtin::PerMeshBuffer);
        RegisterSRV(Builtin::PerMaterialDataBuffer);
        RegisterSRV("Builtin::VisUtil::MaterialOffsetBuffer");

        RegisterUAV("Builtin::VisUtil::MaterialWriteCursorBuffer");
        RegisterUAV("Builtin::VisUtil::PixelListBuffer");

		std::vector<GloballyIndexedResource*> visibleClusterResources;
        m_visibleClustersQuery.each([&](flecs::entity e) {
			auto& res = e.get<Components::Resource>();
			auto test = std::static_pointer_cast<GloballyIndexedResource>(res.resource.lock());
            if (test) {
                visibleClusterResources.push_back(test.get());
            }
			});

		if (visibleClusterResources.size() != 1) {
			throw std::runtime_error("BuildPixelListPass: Expected exactly one visible cluster buffer resource.");
		}

		m_visibleClusterBufferSRVIndex = visibleClusterResources[0]->GetSRVInfo(0).slot.index;
    }

    PassReturn Execute(RenderContext& ctx) override {
        auto& pm = PSOManager::GetInstance();
        auto& cl = ctx.commandList;

        cl.SetDescriptorHeaps(ctx.textureDescriptorHeap.GetHandle(), ctx.samplerDescriptorHeap.GetHandle());
        cl.BindLayout(pm.GetComputeRootSignature().GetHandle());
        cl.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        BindResourceDescriptorIndices(cl, m_pso.GetResourceDescriptorSlots());

		// Set per-pass root constants
        unsigned int miscRootConstants[NumMiscUintRootConstants] = {};
        miscRootConstants[VISBUF_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClusterBufferSRVIndex;
        cl.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &miscRootConstants);

        const uint32_t gsX = 8, gsY = 8;
        uint32_t x = (ctx.renderResolution.x + gsX - 1) / gsX;
        uint32_t y = (ctx.renderResolution.y + gsY - 1) / gsY;
        cl.Dispatch(x, y, 1);

        return {};
    }

    void Cleanup() override {}

private:
    PipelineState m_pso;
	flecs::query<flecs::entity> m_visibleClustersQuery;
    uint32_t m_visibleClusterBufferSRVIndex = 0;
};