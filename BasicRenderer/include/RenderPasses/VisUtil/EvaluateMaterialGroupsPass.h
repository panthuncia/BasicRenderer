#pragma once
#include <vector>
#include <cstdint>
#include <bit>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/MaterialManager.h"
#include "Render/RenderContext.h"
#include "Render/MaterialStateArtifacts.h"
#include "Render/IndirectCommand.h"
#include "Render/OutputTypes.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/ShaderVariantRequestService.h"
#include "Render/ProducerPassServices.h"
#include "Resources/Buffers/PagePool.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "RenderPasses/PreparedComputeDispatch.h"

struct EvaluateMaterialGroupsBindings {
    org::ResourceBindingToken visibleClusters, visibleClusterTransformIndices;
    org::ResourceBindingToken reyesDiceQueue, reyesTessTableConfigs;
    org::ResourceBindingToken reyesTessTableVertices, reyesTessTableTriangles;
    bool hasReyesDiceQueue = false, hasReyesTessTables = false;
    uint32_t patchVisibilityIndexBase = 0;
};

class EvaluateMaterialGroupsPass : public org::TypedRenderGraphPass<EvaluateMaterialGroupsPass,
    br::render::PreparedComputeIndirectSequence, EvaluateMaterialGroupsBindings> {
public:
    EvaluateMaterialGroupsPass(ProducerPassServices& services, bool terrainRvtEnabled)
        : m_services(services), m_terrainRvtEnabled(terrainRvtEnabled) {
        if (!m_services.IsValid()) throw std::invalid_argument("EvaluateMaterialGroupsPass requires producer services");
        auto& ecsWorld = m_services.ecs->GetWorld();

        // Global LOD extension visibility buffer tag
        auto visBufferTag = ecsWorld.component<CLodExtensionVisibilityBufferTag>();

        // Query for entities with the visibility buffer tag
        m_visibleClustersQuery =
            ecsWorld.query_builder<>()
            .with<CLodExtensionTypeTag>(visBufferTag)
            .with<VisibleClustersBufferTag>()
            .build();

        m_visibleClusterTransformIndicesQuery =
            ecsWorld.query_builder<>()
            .with<CLodExtensionTypeTag>(visBufferTag)
            .with<VisibleClusterTransformIndicesBufferTag>()
            .build();

        m_reyesDiceQueueQuery =
            ecsWorld.query_builder<>()
            .with<CLodExtensionTypeTag>(visBufferTag)
            .with<CLodReyesDiceQueueTag>()
            .build();

        m_reyesTessTableConfigsQuery =
            ecsWorld.query_builder<>()
            .with<CLodExtensionTypeTag>(visBufferTag)
            .with<CLodReyesTessTableConfigsTag>()
            .build();

        m_reyesTessTableVerticesQuery =
            ecsWorld.query_builder<>()
            .with<CLodExtensionTypeTag>(visBufferTag)
            .with<CLodReyesTessTableVerticesTag>()
            .build();

        m_reyesTessTableTrianglesQuery =
            ecsWorld.query_builder<>()
            .with<CLodExtensionTypeTag>(visBufferTag)
            .with<CLodReyesTessTableTrianglesTag>()
            .build();

        m_slabResourceGroup = m_services.clodSlabResources;
    }

    EvaluateMaterialGroupsBindings Declare(org::PassBuilder& builder) {
        RefreshResourcePointers();
        EvaluateMaterialGroupsBindings bindings{};
        bindings.visibleClusters = builder.BindShaderResource(m_visibleClusterResource);
        bindings.visibleClusterTransformIndices = builder.BindShaderResource(m_visibleClusterTransformIndicesResource);
        if (m_reyesDiceQueueResource) {
            bindings.reyesDiceQueue = builder.BindShaderResource(m_reyesDiceQueueResource);
            bindings.hasReyesDiceQueue = true;
        }
        if (m_reyesTessTableConfigsResource && m_reyesTessTableVerticesResource && m_reyesTessTableTrianglesResource) {
            bindings.reyesTessTableConfigs = builder.BindShaderResource(m_reyesTessTableConfigsResource);
            bindings.reyesTessTableVertices = builder.BindShaderResource(m_reyesTessTableVerticesResource);
            bindings.reyesTessTableTriangles = builder.BindShaderResource(m_reyesTessTableTrianglesResource);
            bindings.hasReyesTessTables = true;
        }
        bindings.patchVisibilityIndexBase = m_patchVisibilityIndexBase;
        builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
        auto* b = &builder;
        // TODO(async-state-coherence): CLOD visibility/mesh metadata is still sourced
        // from live manager resources while draw and material tables can come from
        // PublishedRendererState. Select exact generations in one transaction.
        b->WithShaderResource(ECSResourceResolver(m_visibleClustersQuery));
        b->WithShaderResource(ECSResourceResolver(m_visibleClusterTransformIndicesQuery));
    	b->WithShaderResource(ECSResourceResolver(m_reyesDiceQueueQuery));
    	b->WithShaderResource(ECSResourceResolver(m_reyesTessTableConfigsQuery));
    	b->WithShaderResource(ECSResourceResolver(m_reyesTessTableVerticesQuery));
    	b->WithShaderResource(ECSResourceResolver(m_reyesTessTableTrianglesQuery));

        if (m_slabResourceGroup) {
            b->WithShaderResource(ResourceGroupResolver(m_slabResourceGroup));
        }

        b->WithShaderResource("Builtin::VisUtil::PixelListBuffer",
            Builtin::PrimaryCamera::VisibilityTexture,
            Builtin::PrimaryCamera::LinearDepthMap,
            //Builtin::PrimaryCamera::VisibleClusterTable,
            Builtin::PerMeshInstanceBuffer,
            Builtin::InstanceDrawRecordBuffer,
            Builtin::PerInstanceTransformBuffer,
            Builtin::PerObjectBuffer,
            Builtin::PerMeshBuffer,
            Builtin::CameraBuffer,
            Builtin::NormalMatrixBuffer,
            Builtin::SkeletonResources::InverseBindMatrices,
            Builtin::SkeletonResources::BoneTransforms,
            Builtin::SkeletonResources::SkinningInstanceInfo,
            Builtin::PerMaterialDataBuffer,
            "Builtin::PerMaterialEvalDataBuffer",
            Builtin::Terrain::Sets,
            Builtin::Terrain::Layers,
            Builtin::Terrain::StochasticLayers,
            Builtin::Terrain::LayerRefs,
            Builtin::Terrain::Regions,
            Builtin::Terrain::WeightBlocks,
            Builtin::Terrain::TextureGroup,
            Builtin::Material::TextureStreamingMetadataBuffer,
            Builtin::CLod::Offsets,
			Builtin::CLod::GroupChunks,
			Builtin::CLod::Groups,
            Builtin::CLod::GroupPageMap,
            Builtin::CLod::MeshMetadata,
            Builtin::CLod::AssemblyTransforms,
            Builtin::CLod::AssemblyBoneRemaps,
            Builtin::CLod::AssemblyBoneRemapIndices,
            Builtin::SkeletonResources::InverseSkinMatrices,
            Builtin::PerMaterialOpenPBRDataBuffer)
            .WithUnorderedAccess(Builtin::Surface::BaseColorOpacity,
                Builtin::Surface::NormalRoughness,
                Builtin::Surface::SpecularAo,
                Builtin::Surface::Emissive,
                Builtin::Surface::Motion,
                Builtin::Surface::Payload0,
                Builtin::Surface::Payload1,
                Builtin::Surface::Identity,
                Builtin::Surface::Records,
                Builtin::DebugVisualization,
				Builtin::Material::TextureStreamingFeedbackBuffer)
    	.WithConstantBuffer(Builtin::PerFrameBuffer);

        if (m_terrainRvtEnabled) {
            b->WithShaderResource(
                Builtin::Terrain::RvtInfo,
                Builtin::Terrain::RvtClipInfos,
                Builtin::Terrain::RvtPageTable,
                Builtin::Terrain::RvtPageKeys,
                Builtin::Terrain::RvtPhysicalPageOwner,
                Builtin::Terrain::RvtPhysicalPageAtlas,
                Builtin::Terrain::RvtHeightResidentCache,
                Builtin::Terrain::RvtHeightAtlas,
                Builtin::Terrain::RvtAlbedoAtlas,
                Builtin::Terrain::RvtNormalAtlas,
                Builtin::Terrain::RvtMaterialAtlas)
                .WithUnorderedAccess(
                    Builtin::Terrain::RvtRequestMasks,
                    Builtin::Terrain::RvtRequestList,
                    Builtin::Terrain::RvtCounters,
                    Builtin::Terrain::RvtStats);
        }
        b->WithIndirectArguments("Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer");
        return bindings;
    }

    void Initialize() {
        RefreshResourcePointers();
        m_materialEvalCmds = m_resourceRegistryView->RequestPtr<Resource>("Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer");
    }

    void RefreshResourcePointers() {
        std::vector<std::shared_ptr<GloballyIndexedResource>> visibleClusterResources;
        m_visibleClustersQuery.each([&](flecs::entity e) {
            auto& res = e.get<Components::Resource>();
            auto test = std::static_pointer_cast<GloballyIndexedResource>(res.resource.lock());
            if (test) {
                visibleClusterResources.push_back(std::move(test));
            }
            const auto capacity = e.get<CLodVisibleClusterCapacity>();
            m_patchVisibilityIndexBase = CLodReyesPatchVisibilityIndexBase(capacity.maxVisibleClusters);
            });

        if (visibleClusterResources.size() != 1) {
            throw std::runtime_error("EvaluateMaterialGroupsPass: Expected exactly one visible cluster buffer resource.");
        }

        m_visibleClusterResource = std::move(visibleClusterResources[0]);
        m_visibleClusterTransformIndicesResource.reset();
        m_reyesDiceQueueResource.reset();
        m_reyesTessTableConfigsResource.reset();
        m_reyesTessTableVerticesResource.reset();
        m_reyesTessTableTrianglesResource.reset();

        std::vector<std::shared_ptr<GloballyIndexedResource>> visibleClusterTransformIndexResources;
        m_visibleClusterTransformIndicesQuery.each([&](flecs::entity e) {
            if (const auto res = e.try_get<Components::Resource>(); res) {
                if (const auto resource = std::static_pointer_cast<GloballyIndexedResource>(res->resource.lock()); resource) {
                    visibleClusterTransformIndexResources.push_back(std::move(resource));
                }
            }
        });
        if (visibleClusterTransformIndexResources.size() != 1) {
            throw std::runtime_error("EvaluateMaterialGroupsPass: Expected exactly one visible cluster transform-index resource.");
        }
        m_visibleClusterTransformIndicesResource = std::move(visibleClusterTransformIndexResources[0]);

        std::vector<std::shared_ptr<GloballyIndexedResource>> reyesDiceQueueResources;
        m_reyesDiceQueueQuery.each([&](flecs::entity e) {
            if (const auto res = e.try_get<Components::Resource>(); res) {
                if (const auto test = std::static_pointer_cast<GloballyIndexedResource>(res->resource.lock()); test) {
                    reyesDiceQueueResources.push_back(std::move(test));
                }
            }
            });
        if (reyesDiceQueueResources.size() == 1) {
            m_reyesDiceQueueResource = std::move(reyesDiceQueueResources[0]);
        }

        std::vector<std::shared_ptr<GloballyIndexedResource>> reyesTessTableConfigResources;
        m_reyesTessTableConfigsQuery.each([&](flecs::entity e) {
            if (const auto res = e.try_get<Components::Resource>(); res) {
                if (const auto resource = std::static_pointer_cast<GloballyIndexedResource>(res->resource.lock()); resource) {
                    reyesTessTableConfigResources.push_back(std::move(resource));
                }
            }
        });
        if (reyesTessTableConfigResources.size() == 1) {
            m_reyesTessTableConfigsResource = std::move(reyesTessTableConfigResources[0]);
        }

        std::vector<std::shared_ptr<GloballyIndexedResource>> reyesTessTableVertexResources;
        m_reyesTessTableVerticesQuery.each([&](flecs::entity e) {
            if (const auto res = e.try_get<Components::Resource>(); res) {
                if (const auto resource = std::static_pointer_cast<GloballyIndexedResource>(res->resource.lock()); resource) {
                    reyesTessTableVertexResources.push_back(std::move(resource));
                }
            }
        });
        if (reyesTessTableVertexResources.size() == 1) {
            m_reyesTessTableVerticesResource = std::move(reyesTessTableVertexResources[0]);
        }

        std::vector<std::shared_ptr<GloballyIndexedResource>> reyesTessTableTriangleResources;
        m_reyesTessTableTrianglesQuery.each([&](flecs::entity e) {
            if (const auto res = e.try_get<Components::Resource>(); res) {
                if (const auto resource = std::static_pointer_cast<GloballyIndexedResource>(res->resource.lock()); resource) {
                    reyesTessTableTriangleResources.push_back(std::move(resource));
                }
            }
        });
        if (reyesTessTableTriangleResources.size() == 1) {
            m_reyesTessTableTrianglesResource = std::move(reyesTessTableTriangleResources[0]);
        }
    }

    br::render::PreparedComputeIndirectSequence Prepare(const EvaluateMaterialGroupsBindings& bindings,
        const org::PassPrepareContext& preparation) const {
        const auto* context = preparation.preparationData->Get<UpdateContext>();
        const auto materialState = context->publishedRendererState
            ? context->publishedRendererState->materials.payload.Get<br::render::PublishedMaterialState>() : nullptr;
        if (!materialState) return {};
        br::render::PreparedComputeIndirectSequence data{};
        data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
        data.commandSignature = preparation.CaptureCommandSignature(m_services.commandSignatures->CaptureMaterialEvaluationCommandSignature());
        data.argumentsReference = preparation.CaptureResource(m_materialEvalCmds->GetGlobalResourceID());
        const uint64_t stride = sizeof(MaterialEvaluationIndirectCommand);
        const bool terrainEvaluation = m_services.settings->getSettingGetter<bool>("enableTerrainRegionMaterialEvaluation")();
        const auto outputType = m_services.settings->getSettingGetter<unsigned int>("outputType")();
        for (std::size_t activeIndex = 0; activeIndex < materialState->activeCompileFlags.size(); ++activeIndex) {
            const auto flags = materialState->activeCompileFlags[activeIndex];
            if (terrainEvaluation && (flags & MaterialCompileFlags::MaterialCompileTerrain) != 0) continue;
            if (activeIndex >= materialState->activeCompileFlagSlots.size()) continue;
            const unsigned int slot = materialState->activeCompileFlagSlots[activeIndex];
            if (slot >= materialState->compileFlagSlotsUsed) continue;
            auto shaderKey = GetMaterialEvaluationShaderKey(flags);
            if (outputType == OutputType::COLOR) shaderKey |= MaterialCompileFlags::MaterialCompileMaterialEvalColorOnly;
            const PipelineState* pso = m_services.pipelines->TryGetMaterialEvalPSO(shaderKey);
            if (!pso) continue;
            const uint64_t argOffset = static_cast<uint64_t>(slot) * stride;
            if (auto* buffer = dynamic_cast<BufferBase*>(m_materialEvalCmds);
                buffer && argOffset + stride > buffer->GetBufferSize()) continue;
            auto capture = preparation;
            capture.captureDescriptorIndices = [this](const PipelineResources& resources) {
                return CaptureMaterialResourceDescriptorIndices(resources);
            };
            auto program = capture.CaptureProgramBinding(*pso);
            br::render::PreparedComputeIndirectSequence::Step step{};
            step.program = program.program;
            step.descriptorIndices = std::move(program.descriptorIndices);
            step.constants[VISBUF_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = preparation.ResolveView(
                bindings.visibleClusters, {org::BindlessViewKind::ShaderResource}).index;
            step.constants[VISBUF_VISIBLE_CLUSTER_TRANSFORM_INDICES_DESCRIPTOR_INDEX] = preparation.ResolveView(
                bindings.visibleClusterTransformIndices, {org::BindlessViewKind::ShaderResource}).index;
            step.constants[VISBUF_REYES_DICE_QUEUE_DESCRIPTOR_INDEX] = bindings.hasReyesDiceQueue
                ? preparation.ResolveView(bindings.reyesDiceQueue, {org::BindlessViewKind::ShaderResource}).index : 0xFFFFFFFFu;
            step.constants[VISBUF_REYES_PATCH_INDEX_BASE] = bindings.patchVisibilityIndexBase;
            step.constants[VISBUF_REYES_TESS_TABLE_CONFIGS_DESCRIPTOR_INDEX] = bindings.hasReyesTessTables
                ? preparation.ResolveView(bindings.reyesTessTableConfigs, {org::BindlessViewKind::ShaderResource}).index : 0xFFFFFFFFu;
            step.constants[VISBUF_REYES_TESS_TABLE_VERTICES_DESCRIPTOR_INDEX] = bindings.hasReyesTessTables
                ? preparation.ResolveView(bindings.reyesTessTableVertices, {org::BindlessViewKind::ShaderResource}).index : 0xFFFFFFFFu;
            step.constants[VISBUF_REYES_TESS_TABLE_TRIANGLES_DESCRIPTOR_INDEX] = bindings.hasReyesTessTables
                ? preparation.ResolveView(bindings.reyesTessTableTriangles, {org::BindlessViewKind::ShaderResource}).index : 0xFFFFFFFFu;
            step.constants[VISBUF_REYES_USE_NORMAL_MAPS] = CLodReyesUseNormalMaps() ? 1u : 0u;
            step.constants[VISBUF_REYES_TERRAIN_NORMAL_BLEND_AS_UINT] = std::bit_cast<uint32_t>(CLodReyesTerrainNormalBlend());
            step.constants[VISBUF_REYES_TERRAIN_NORMAL_MIP_BIAS] = CLodReyesTerrainNormalMipBias();
            step.constants[VISBUF_REYES_OBJECT_NORMAL_MAP_BLEND_AS_UINT] = std::bit_cast<uint32_t>(CLodReyesObjectNormalMapBlend());
            step.argumentsOffset = argOffset; data.steps.push_back(std::move(step));
        }
        return data;
    }

    static void Record(const EvaluateMaterialGroupsBindings&, const br::render::PreparedComputeIndirectSequence& data,
        org::PassRecordContext& recording) {
        br::render::RecordPreparedComputeIndirectSequence(data, recording);
    }

    void ShutdownPass() {
        m_visibleClustersQuery = {};
        m_visibleClusterTransformIndicesQuery = {};
        m_reyesDiceQueueQuery = {};
        m_reyesTessTableConfigsQuery = {};
        m_reyesTessTableVerticesQuery = {};
        m_reyesTessTableTrianglesQuery = {};
        m_slabResourceGroup.reset();
        m_materialEvalCmds = nullptr;
    }

private:
    ProducerPassServices& m_services;
    std::vector<unsigned int> CaptureMaterialResourceDescriptorIndices(const PipelineResources& resources) const {
        std::vector<unsigned int> indices;
        indices.reserve(resources.mandatoryResourceDescriptorSlots.size() + resources.optionalResourceDescriptorSlots.size());
        for (const auto& binding : resources.mandatoryResourceDescriptorSlots) {
            const bool allowMissing = !m_terrainRvtEnabled && binding.name.starts_with("Builtin::Terrain::Rvt");
            indices.push_back(m_resourceDescriptorIndexHelper->GetResourceDescriptorIndex(binding, allowMissing));
        }
        for (const auto& binding : resources.optionalResourceDescriptorSlots)
            indices.push_back(m_resourceDescriptorIndexHelper->GetResourceDescriptorIndex(binding, true));
        return indices;
    }

    bool m_terrainRvtEnabled = false;
    Resource* m_materialEvalCmds;
    flecs::query<> m_visibleClustersQuery;
    flecs::query<> m_visibleClusterTransformIndicesQuery;
    flecs::query<> m_reyesDiceQueueQuery;
    flecs::query<> m_reyesTessTableConfigsQuery;
    flecs::query<> m_reyesTessTableVerticesQuery;
    flecs::query<> m_reyesTessTableTrianglesQuery;
    std::shared_ptr<ResourceGroup> m_slabResourceGroup;
    std::shared_ptr<GloballyIndexedResource> m_visibleClusterResource;
    std::shared_ptr<GloballyIndexedResource> m_visibleClusterTransformIndicesResource;
    std::shared_ptr<GloballyIndexedResource> m_reyesDiceQueueResource;
    std::shared_ptr<GloballyIndexedResource> m_reyesTessTableConfigsResource;
    std::shared_ptr<GloballyIndexedResource> m_reyesTessTableVerticesResource;
    std::shared_ptr<GloballyIndexedResource> m_reyesTessTableTrianglesResource;
    uint32_t m_patchVisibilityIndexBase = 0u;
};
