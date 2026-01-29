#pragma once

#include <rhi.h>

#include "Render/RenderGraph.h"

class CLodExtension final : public RenderGraph::IRenderGraphExtension {
public:
	explicit CLodExtension() {
	}

	void OnRegistryReset(ResourceRegistry* reg) override {

	}

	void GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override {
		// Add the hierarchical culling pass
		RenderGraph::ExternalPassDesc cullPassDesc;
		cullPassDesc.type = RenderGraph::PassType::Compute;
		cullPassDesc.name = "CLod::HierarchialCullingPass";
		HierarchialCullingPassInputs cullPassInputs;
		cullPassInputs.isFirstPass = true; // For now, always true
		cullPassDesc.pass = std::make_shared<HierarchialCullingPass>(cullPassInputs);
		cullPassDesc.where = RenderGraph::ExternalInsertPoint::After("SkinningPass");
		outPasses.push_back(std::move(cullPassDesc));
	}

private:

	// ---------------------------------------------------------------
	// Hierarchial Culling Pass
	// ---------------------------------------------------------------

    struct ObjectCullRecord
    {
        uint viewDataIndex; // One record per view *...
        uint activeDrawSetIndicesSRVIndex; // One record per draw set
        uint activeDrawCount;
        uint pad0; // Padding for 16-byte alignment
        uint dispatchGridX; // Drives dispatch size
        uint dispatchGridY;
        uint dispatchGridZ;
        uint pad1; // Padding for 16-byte alignment
    };

    struct HierarchialCullingPassInputs {
        bool isFirstPass;

        friend bool operator==(const HierarchialCullingPassInputs&, const HierarchialCullingPassInputs&) = default;
    };

    inline rg::Hash64 HashValue(const HierarchialCullingPassInputs& i) {
        std::size_t seed = 0;

        boost::hash_combine(seed, i.isFirstPass);
        return seed;
    }

    class HierarchialCullingPass : public ComputePass {
    public:
        HierarchialCullingPass(HierarchialCullingPassInputs inputs) {
            CreatePipelines(
                DeviceManager::GetInstance().GetDevice(),
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_workGraph,
                m_createCommandPipelineState);
            auto memSize = m_workGraph->GetRequiredScratchMemorySize();
            m_scratchBuffer = Buffer::CreateShared( // TODO: Make a way for the graph to provide things like this, to allow for aliasing
                rhi::HeapType::DeviceLocal,
                memSize,
                true);
            m_scratchBuffer->ApplyMetadataComponentBundle(
                EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Work graph scratch buffer" }));
        }

        ~HierarchialCullingPass() {
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) {
            auto ecsWorld = ECSManager::GetInstance().GetWorld();
            flecs::query<> drawSetIndicesQuery = ecsWorld.query_builder<>()
                .with<Components::IsActiveDrawSetIndices>()
                .with<Components::ParticipatesInPass>(flecs::Wildcard)
                .build();
            builder->WithUnorderedAccess(m_scratchBuffer,
                Builtin::VisibleClusterBuffer,
                Builtin::VisibleClusterCounter,
                "Builtin::CLod::RasterBucketsHistogramIndirectCommand")
                .WithShaderResource(Builtin::IndirectCommandBuffers::Master,
                    Builtin::CLod::Offsets,
                    Builtin::CLod::Groups,
                    Builtin::CLod::Children,
                    Builtin::CLod::ChildLocalMeshletIndices,
                    Builtin::CLod::Nodes,
                    Builtin::CullingCameraBuffer,
                    Builtin::PerMeshInstanceBuffer,
                    Builtin::PerObjectBuffer)
                .WithShaderResource(ECSResourceResolver(drawSetIndicesQuery));
        }

        void Setup() override {
            RegisterSRV(Builtin::IndirectCommandBuffers::Master);
            RegisterSRV(Builtin::CLod::Offsets);
            RegisterSRV(Builtin::CLod::Groups);
            RegisterSRV(Builtin::CLod::Children);
            RegisterSRV(Builtin::CLod::ChildLocalMeshletIndices);
            RegisterSRV(Builtin::CullingCameraBuffer);
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerObjectBuffer);
            RegisterSRV(Builtin::CLod::Nodes);

            RegisterUAV(Builtin::VisibleClusterBuffer);
            RegisterUAV(Builtin::VisibleClusterCounter);
            RegisterUAV("Builtin::CLod::RasterBucketsHistogramIndirectCommand");

            m_visibleClusterCounter = m_resourceRegistryView->RequestHandle(Builtin::VisibleClusterCounter);
        }

        PassReturn Execute(RenderContext& context) override {
            auto& commandList = context.commandList;

            // Set the descriptor heaps
            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

            std::vector<ObjectCullRecord> cullRecords;

            // Build cull records for all indirect buffers
            context.indirectCommandBufferManager->ForEachIndirectBuffer([&](uint64_t view,
                MaterialCompileFlags flags,
                const IndirectWorkload& wl)
                {
                    if (wl.count == 0) {
                        return;
                    }
                    auto viewInfo = context.viewManager->Get(view);
                    auto cameraBufferIndex = viewInfo->gpu.cameraBufferIndex;
                    ObjectCullRecord record{};
                    record.viewDataIndex = cameraBufferIndex;
                    record.activeDrawSetIndicesSRVIndex = context.objectManager->GetActiveDrawSetIndices(flags)->GetSRVInfo(0).slot.index;
                    record.activeDrawCount = wl.count;
                    // Calculate dispatch grid size (assuming 64 threads per group)
                    record.dispatchGridX = static_cast<uint>((wl.count + 63) / 64);
                    record.dispatchGridY = 1;
                    record.dispatchGridZ = 1;
                    cullRecords.push_back(record);
                });

            commandList.SetWorkGraph(m_workGraph->GetHandle(), m_scratchBuffer->GetAPIResource().GetHandle(), true); // Reset every time for now

            BindResourceDescriptorIndices(commandList, m_pipelineResources);

            rhi::WorkGraphDispatchDesc dispatchDesc{};
            dispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::NodeCpuInput;
            dispatchDesc.nodeCpuInput.entryPointIndex = 0; // ObjectCull node
            dispatchDesc.nodeCpuInput.pRecords = cullRecords.data();
            dispatchDesc.nodeCpuInput.numRecords = static_cast<uint32_t>(cullRecords.size());
            dispatchDesc.nodeCpuInput.recordByteStride = sizeof(ObjectCullRecord);

            // Builds list of visible clusters
            commandList.DispatchWorkGraph(dispatchDesc);

            // UAV barrier on the visible cluster counter
            rhi::BufferBarrier barrier{};
            barrier.buffer = m_resourceRegistryView->Resolve<Resource>(m_visibleClusterCounter)->GetAPIResource().GetHandle();
            barrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
            barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            barrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
            barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
            rhi::BarrierBatch bufferBarriers{};
            bufferBarriers.buffers = rhi::Span<rhi::BufferBarrier>(&barrier, 1);
            commandList.Barriers(bufferBarriers);

            // Create indirect command buffer for LOD rasterization
            BindResourceDescriptorIndices(commandList, m_createCommandPipelineState.GetResourceDescriptorSlots());
            commandList.BindPipeline(m_createCommandPipelineState.GetAPIPipelineState().GetHandle());
            commandList.Dispatch(1, 1, 1); // Single thread group, one thread

            return {};
        }

        void Update(const UpdateContext& context) override {
            uint32_t zero = 0u;
            BUFFER_UPLOAD(&zero, sizeof(uint32_t), UploadManager::UploadTarget::FromHandle(m_visibleClusterCounter), 0);
        }

        void Cleanup() override {

        }

    private:
        PipelineResources m_pipelineResources;
        rhi::WorkGraphPtr m_workGraph;
        PipelineState m_createCommandPipelineState;
        std::shared_ptr<Buffer> m_scratchBuffer;
        ResourceRegistry::RegistryHandle m_visibleClusterCounter;

        rhi::Result CreatePipelines(
            rhi::Device device,
            rhi::PipelineLayoutHandle globalRootSignature,
            rhi::WorkGraphPtr& outGraph,
            PipelineState& outCreateCommandPipeline)
        {
            // Compile the work-graph library
            ShaderLibraryInfo libInfo(L"shaders/workGraphCulling.hlsl", L"lib_6_8");
            auto compiled = PSOManager::GetInstance().CompileShaderLibrary(libInfo);
            m_pipelineResources = compiled.resourceDescriptorSlots;

            rhi::ShaderBinary libDxil{
                compiled.libraryBlob->GetBufferPointer(),
                static_cast<uint32_t>(compiled.libraryBlob->GetBufferSize())
            };

            // Export the node shader symbols from the library
            // These are the *export names* (function symbols), not NodeID strings.
            std::array<rhi::ShaderExportDesc, 5> exports = { {
                { "WG_ObjectCull",   nullptr },
                { "WG_Traverse",     nullptr },
                { "WG_ClusterCullBuckets",  nullptr },
                //{ "WG_Output", nullptr }
            } };

            rhi::ShaderLibraryDesc library{};
            library.dxil = libDxil;
            library.exports = rhi::Span<rhi::ShaderExportDesc>(exports.data(), static_cast<uint32_t>(exports.size()));

            std::array<rhi::ShaderLibraryDesc, 1> libraries = { library };

            // Entry point is by NodeID (the [NodeID("ObjectCull")] in HLSL)
            std::array<rhi::NodeIDDesc, 1> entrypoints = { {
                { "ObjectCull", 0 }
            } };

            // Build the work graph desc
            rhi::WorkGraphDesc wg{};
            wg.programName = "HierarchialCulling";
            wg.flags = rhi::WorkGraphFlags::WorkGraphFlagsIncludeAllAvailableNodes;
            wg.globalRootSignature = globalRootSignature;
            wg.libraries = rhi::Span<rhi::ShaderLibraryDesc>(libraries.data(), static_cast<uint32_t>(libraries.size()));
            wg.entrypoints = rhi::Span<rhi::NodeIDDesc>(entrypoints.data(), static_cast<uint32_t>(entrypoints.size()));
            wg.allowStateObjectAdditions = false;
            wg.debugName = "HierarchialCullingWG";

            // Create
            return device.CreateWorkGraph(wg, outGraph);

            // Pipeline to create indirect command
            outCreateCommandPipeline = PSOManager::GetInstance().MakeComputePipeline(
                globalRootSignature,
                L"shaders/createRasterizeClustersCommand.hlsl",
                L"CreateRasterizeClustersCommand",
                {},
                "HierarchialLODRasterizeCommandCreation");
        }
    };

	// --------------------------------------------------------------
	// Raster Bucket Histogram Pass
	// --------------------------------------------------------------

    class RasterBucketHistogramPass : public ComputePass {
    public:
        RasterBucketHistogramPass() {
            CreatePipelines(
                DeviceManager::GetInstance().GetDevice(),
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
                m_histogramPipeline);

            // Used by the cluster rasterization pass
            rhi::IndirectArg rasterizeClustersArgs[] = {
                {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { MiscUintRootSignatureIndex, 0, 2 } } },
                {.kind = rhi::IndirectArgKind::DispatchMesh }
            };

            auto device = DeviceManager::GetInstance().GetDevice();

            auto result = device.CreateCommandSignature(
                rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(rasterizeClustersArgs, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
                PSOManager::GetInstance().GetComputeRootSignature().GetHandle(), m_histogramCommandSignature);
        }

        ~RasterBucketHistogramPass() {
        }

        void DeclareResourceUsages(ComputePassBuilder* builder) {
            builder->WithShaderResource(
                Builtin::VisibleClusterBuffer,
                Builtin::VisibleClusterCounter)
                .WithIndirectArguments("Builtin::CLod::RasterBucketsHistogramIndirectCommand")
                .WithUnorderedAccess("Builtin::CLod::RasterBucketsClusterCountBuffer");
        }

        void Setup() override {

            RegisterSRV(Builtin::VisibleClusterBuffer);
            RegisterSRV(Builtin::VisibleClusterCounter);
            RegisterUAV("Builtin::CLod::RasterBucketsClusterCountBuffer");

            m_rasterBucketHistogramIndirectCommandsResource = m_resourceRegistryView->RequestPtr<Resource>("Builtin::CLod::RasterBucketsHistogramIndirectCommand");
        }

        PassReturn Execute(RenderContext& context) override {
            auto& commandList = context.commandList;

            // Set the descriptor heaps
            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

            commandList.BindPipeline(m_histogramPipeline.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_histogramPipeline.GetResourceDescriptorSlots());

            // Single-dispatch ExecuteIndirect
            commandList.ExecuteIndirect(m_histogramCommandSignature->GetHandle(), m_rasterBucketHistogramIndirectCommandsResource->GetAPIResource().GetHandle(), 0, {}, 0, 1);

            return {};
        }

        void Update(const UpdateContext& context) override {
        }

        void Cleanup() override {

        }

    private:
        PipelineState m_histogramPipeline;
        rhi::CommandSignaturePtr m_histogramCommandSignature;
        Resource* m_rasterBucketHistogramIndirectCommandsResource = nullptr;

        rhi::Result CreatePipelines(
            rhi::Device device,
            rhi::PipelineLayoutHandle globalRootSignature,
            PipelineState& outHistogramPipeline)
        {
            outHistogramPipeline = PSOManager::GetInstance().MakeComputePipeline(
                globalRootSignature,
                L"Shaders/ClusterLOD/RasterBucketHistogramCS.hlsl",
                L"RasterBucketHistogramCSMain");
        }
    };

	// --------------------------------------------------------------
	// Cluster Rasterization Pass
	// --------------------------------------------------------------

    struct ClusterRasterizationPassInputs {
        bool wireframe;
        bool meshShaders;
        bool clearGbuffer;

        friend bool operator==(const ClusterRasterizationPassInputs&, const ClusterRasterizationPassInputs&) = default;
    };

    inline rg::Hash64 HashValue(const ClusterRasterizationPassInputs& i) {
        std::size_t seed = 0;

        boost::hash_combine(seed, i.wireframe);
        boost::hash_combine(seed, i.meshShaders);
        boost::hash_combine(seed, i.clearGbuffer);
        return seed;
    }

    class ClusterRasterizationPass : public RenderPass {
    public:
        ClusterRasterizationPass() {
            auto& ecsWorld = ECSManager::GetInstance().GetWorld();
            m_meshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::PerPassMeshes>()
                .with<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::GBufferPass))
                .cached().cache_kind(flecs::QueryCacheAll).build();
        }

        ~ClusterRasterizationPass() {
        }

        void DeclareResourceUsages(RenderPassBuilder* builder) {
            auto input = Inputs<ClusterRasterizationPassInputs>();
            m_wireframe = input.wireframe;
            m_meshShaders = input.meshShaders;
            m_clearGbuffer = input.clearGbuffer;

            builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS,
                Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer,
                Builtin::PerObjectBuffer,
                Builtin::NormalMatrixBuffer,
                Builtin::PerMeshBuffer,
                Builtin::PerMeshInstanceBuffer,
                Builtin::PerMaterialDataBuffer,
                Builtin::PostSkinningVertices,
                Builtin::CameraBuffer)
                .WithRenderTarget(
                    Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 })
                )
                .WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
                .IsGeometryPass();
            builder->WithRenderTarget(
                Builtin::PrimaryCamera::VisibilityTexture);

            if (m_meshShaders) {
                builder->WithShaderResource(Builtin::PerMeshBuffer, Builtin::PrimaryCamera::MeshletBitfield);
                auto& ecsWorld = ECSManager::GetInstance().GetWorld();
                flecs::query<> indirectQuery = ecsWorld.query_builder<>()
                    .with<Components::IsIndirectArguments>()
                    .with<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::GBufferPass)) // Query for command lists that participate in this pass
                    //.cached().cache_kind(flecs::QueryCacheAll)
                    .build();
                builder->WithIndirectArguments(ECSResourceResolver(indirectQuery));
            }
        }

        void Setup() override {

            m_pPrimaryDepthBuffer = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
            m_pVisibilityBuffer = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::VisibilityTexture);

            if (m_meshShaders) {
                RegisterSRV(Builtin::MeshResources::MeshletOffsets);
                RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
                RegisterSRV(Builtin::MeshResources::MeshletTriangles);
            }

            RegisterSRV(Builtin::NormalMatrixBuffer);
            RegisterSRV(Builtin::PostSkinningVertices);
            RegisterSRV(Builtin::PerObjectBuffer);
            RegisterSRV(Builtin::CameraBuffer);
            RegisterSRV(Builtin::PerMeshInstanceBuffer);
            RegisterSRV(Builtin::PerMeshBuffer);
            RegisterSRV(Builtin::PerMaterialDataBuffer);
            RegisterSRV(Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer);
        }

        PassReturn Execute(RenderContext& context) override {
            auto& commandList = context.commandList;

            BeginPass(context);

            SetupCommonState(context, commandList);
            SetCommonRootConstants(context, commandList);

            ExecuteMeshShaderIndirect(context, commandList);

            return {};
        }

        void Cleanup() override {
        }

    private:
        void BeginPass(RenderContext& context) {
            // Build attachments
            rhi::PassBeginInfo p{};
            p.width = context.renderResolution.x;
            p.height = context.renderResolution.y;
            p.debugName = "GBuffer Pass";

            rhi::DepthAttachment da{};
            da.dsv = m_pPrimaryDepthBuffer->GetDSVInfo(0).slot;
            da.depthStore = rhi::StoreOp::Store;

            if (m_clearGbuffer) {
                da.depthLoad = rhi::LoadOp::Clear;
                da.clear.type = rhi::ClearValueType::DepthStencil;
                da.clear.format = rhi::Format::D32_Float;
                da.clear.depthStencil.depth = 1.0f;
                da.clear.depthStencil.stencil = 0;
            }
            else {
                da.depthLoad = rhi::LoadOp::Load;
            }
            p.depth = &da;

            std::vector<rhi::ColorAttachment> colors;

            // visibility buffer
            {
                rhi::ColorAttachment ca{};
                ca.rtv = m_pVisibilityBuffer->GetRTVInfo(0).slot;
                ca.storeOp = rhi::StoreOp::Store;
                ca.loadOp = rhi::LoadOp::Load; // Clearing is handled in a separate pass
                colors.push_back(ca);
            }

            p.colors = { colors.data(), (uint32_t)colors.size() };

            context.commandList.BeginPass(p);
        }
        // Common setup code that doesn't change between techniques
        void SetupCommonState(RenderContext& context, rhi::CommandList& commandList) {

            commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

            commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);

            // Root signature
            commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
        }

        void SetCommonRootConstants(RenderContext& context, rhi::CommandList& commandList) {

        }

        void ExecuteMeshShaderIndirect(RenderContext& context, rhi::CommandList& commandList) {
            // Mesh shading with ExecuteIndirect
            auto& psoManager = PSOManager::GetInstance();

            auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

            // Opaque clusters

        }

    private:

        flecs::query<Components::ObjectDrawInfo, Components::PerPassMeshes> m_meshInstancesQuery;
        bool m_wireframe;
        bool m_meshShaders;
        bool m_clearGbuffer = true;

        PixelBuffer* m_pPrimaryDepthBuffer;
        PixelBuffer* m_pVisibilityBuffer;

        RenderPhase m_renderPhase = Engine::Primary::GBufferPass;
    };

};
