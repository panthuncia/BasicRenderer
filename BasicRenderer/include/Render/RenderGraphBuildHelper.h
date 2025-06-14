#pragma once
#include "Scene/Components.h"
#include "Render/RenderGraph.h"
#include "../../generated/BuiltinResources.h"

// TODO: Find better way of batching these with namespaces
#define MESH_RESOURCE_IDFENTIFIERS Builtin::MeshResources::MeshletBounds, Builtin::MeshResources::MeshletOffsets, Builtin::MeshResources::MeshletVertexIndices, Builtin::MeshResources::MeshletTriangles

void CreateGBufferResources(RenderGraph* graph) {
    // GBuffer resources
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();

    TextureDescription normalsWorldSpaceDesc;
    normalsWorldSpaceDesc.arraySize = 1;
    normalsWorldSpaceDesc.channels = 4;
    normalsWorldSpaceDesc.isCubemap = false;
    normalsWorldSpaceDesc.hasRTV = true;
    normalsWorldSpaceDesc.format = DXGI_FORMAT_R10G10B10A2_UNORM;
    normalsWorldSpaceDesc.generateMipMaps = false;
    normalsWorldSpaceDesc.hasSRV = true;
    normalsWorldSpaceDesc.srvFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
    ImageDimensions dims = { resolution.x, resolution.y, 0, 0 };
    normalsWorldSpaceDesc.imageDimensions.push_back(dims);
    auto normalsWorldSpace = PixelBuffer::Create(normalsWorldSpaceDesc);
    normalsWorldSpace->SetName(L"Normals World Space");
    graph->RegisterResource(Builtin::GBuffer::Normals, normalsWorldSpace);

    std::shared_ptr<PixelBuffer> albedo;
    std::shared_ptr<PixelBuffer> metallicRoughness;
    std::shared_ptr<PixelBuffer> emissive;
    if (deferredRendering) {
        TextureDescription albedoDesc;
        albedoDesc.arraySize = 1;
        albedoDesc.channels = 4;
        albedoDesc.isCubemap = false;
        albedoDesc.hasRTV = true;
        albedoDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        albedoDesc.generateMipMaps = false;
        albedoDesc.hasSRV = true;
        albedoDesc.srvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        ImageDimensions albedoDims = { resolution.x, resolution.y, 0, 0 };
        albedoDesc.imageDimensions.push_back(albedoDims);
        albedo = PixelBuffer::Create(albedoDesc);
        albedo->SetName(L"Albedo");
        graph->RegisterResource(Builtin::GBuffer::Albedo, albedo);

        TextureDescription metallicRoughnessDesc;
        metallicRoughnessDesc.arraySize = 1;
        metallicRoughnessDesc.channels = 2;
        metallicRoughnessDesc.isCubemap = false;
        metallicRoughnessDesc.hasRTV = true;
        metallicRoughnessDesc.format = DXGI_FORMAT_R8G8_UNORM;
        metallicRoughnessDesc.generateMipMaps = false;
        metallicRoughnessDesc.hasSRV = true;
        metallicRoughnessDesc.srvFormat = DXGI_FORMAT_R8G8_UNORM;
        ImageDimensions metallicRoughnessDims = { resolution.x, resolution.y, 0, 0 };
        metallicRoughnessDesc.imageDimensions.push_back(metallicRoughnessDims);
        metallicRoughness = PixelBuffer::Create(metallicRoughnessDesc);
        metallicRoughness->SetName(L"Metallic Roughness");
        graph->RegisterResource(Builtin::GBuffer::MetallicRoughness, metallicRoughness);

        TextureDescription emissiveDesc;
        emissiveDesc.arraySize = 1;
        emissiveDesc.channels = 4;
        emissiveDesc.isCubemap = false;
        emissiveDesc.hasRTV = true;
        emissiveDesc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        emissiveDesc.generateMipMaps = false;
        emissiveDesc.hasSRV = true;
        emissiveDesc.srvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ImageDimensions emissiveDims = { resolution.x, resolution.y, 0, 0 };
        emissiveDesc.imageDimensions.push_back(emissiveDims);
        emissive = PixelBuffer::Create(emissiveDesc);
        emissive->SetName(L"Emissive");
        graph->RegisterResource(Builtin::GBuffer::Emissive, emissive);
    }
}

void BuildOcclusionCullingPipeline(RenderGraph* graph) {

	bool shadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows")();
	bool meshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool wireframeEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();

    graph->BuildRenderPass("ClearLastFrameIndirectDrawUAVsPass") // Clears indirect draws from last frame
        .WithCopyDest(Builtin::IndirectCommandBuffers::Opaque, Builtin::IndirectCommandBuffers::AlphaTest, Builtin::IndirectCommandBuffers::Blend)
        .Build<ClearIndirectDrawCommandUAVsPass>(true);

    graph->BuildRenderPass("ClearMeshletCullingCommandUAVsPass0") // Clear meshlet culling reset command buffers from last frame
        .WithCopyDest(Builtin::IndirectCommandBuffers::MeshletCulling)
        .Build<ClearMeshletCullingCommandUAVsPass>();

    graph->BuildComputePass("BuildOccluderDrawCommandsPass") // Builds draw command list for last frame's occluders
        .WithShaderResource(Builtin::PerObjectBuffer, 
            Builtin::PerMeshBuffer, 
            Builtin::CameraBuffer,
            Builtin::IndirectCommandBuffers::Master,
            Builtin::ActiveDrawSetIndices::Opaque,
            Builtin::ActiveDrawSetIndices::AlphaTest,
            Builtin::ActiveDrawSetIndices::Blend)
        .WithUnorderedAccess(Builtin::IndirectCommandBuffers::Opaque, 
            Builtin::IndirectCommandBuffers::AlphaTest,
            Builtin::IndirectCommandBuffers::MeshletCulling, 
            Builtin::MeshInstanceMeshletCullingBitfieldGroup, 
            Builtin::MeshInstanceOcclusionCullingBitfieldGroup)
        .Build<ObjectCullingPass>(true, true);

    // We need to draw occluder shadows early
    auto drawShadows = graph->RequestResource(Builtin::Shadows::ShadowMaps) != nullptr && shadowsEnabled;
    if (drawShadows) {
        auto shadowOccluderPassBuilder = graph->BuildRenderPass("OccluderShadowPrepass")
            .WithShaderResource(Builtin::PerObjectBuffer, 
                Builtin::NormalMatrixBuffer,
                Builtin::PerMeshBuffer, 
                Builtin::PerMeshInstanceBuffer,
                Builtin::PostSkinningVertices, 
                Builtin::CameraBuffer, 
                Builtin::Light::ViewResourceGroup,
                Builtin::Light::InfoBuffer,
                Builtin::Light::PointLightCubemapBuffer,
                Builtin::Light::DirectionalLightCascadeBuffer,
                Builtin::Light::SpotLightMatrixBuffer)
            .WithRenderTarget(Subresources(Builtin::Shadows::LinearShadowMaps, Mip{ 0, 1 }))
            .WithDepthReadWrite(Builtin::Shadows::ShadowMaps)
            .IsGeometryPass();

        if (meshShadersEnabled) {
            shadowOccluderPassBuilder.WithShaderResource(MESH_RESOURCE_IDFENTIFIERS, Builtin::MeshletCullingBitfieldGroup);
            shadowOccluderPassBuilder.WithIndirectArguments(Builtin::IndirectCommandBuffers::Opaque, 
                Builtin::IndirectCommandBuffers::AlphaTest, 
                Builtin::IndirectCommandBuffers::Blend);
        }
        shadowOccluderPassBuilder.Build<ShadowPass>(wireframeEnabled, meshShadersEnabled, true, true);
    }

    auto occludersPrepassBuilder = graph->BuildRenderPass("OccludersPrepass") // Draws prepass for last frame's occluders
        .WithShaderResource(Builtin::PerObjectBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PostSkinningVertices,
            Builtin::CameraBuffer)
        .WithRenderTarget(
            Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }),
            Builtin::GBuffer::Normals,
            Builtin::GBuffer::MotionVectors);
    if (deferredRendering) {
        occludersPrepassBuilder.WithRenderTarget(
            Builtin::GBuffer::Albedo,
            Builtin::GBuffer::MetallicRoughness,
            Builtin::GBuffer::Emissive);
    }

    occludersPrepassBuilder.WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
        .IsGeometryPass();

    if (meshShadersEnabled) {
        occludersPrepassBuilder.WithShaderResource(Builtin::PerMeshBuffer, Builtin::PrimaryCamera::MeshletBitfield);
        //if (indirect) {
        occludersPrepassBuilder.WithIndirectArguments(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque, Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest);
        //}
    }
    occludersPrepassBuilder.Build<ZPrepass>(
        wireframeEnabled, 
        meshShadersEnabled, 
        true, 
        true);

    // Single-pass downsample on all occluder-only depth maps
    // TODO: Unhandled edge case where HZB is not conservative when downsampling mips with non-even resolutions (bottom/side pixels get dropped)
    auto downsampleBuilder = graph->BuildComputePass("DownsamplePass")
        .WithShaderResource(Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }), Subresources(Builtin::Shadows::LinearShadowMaps, Mip{ 0, 1 }))
        .WithUnorderedAccess(Subresources(Builtin::PrimaryCamera::LinearDepthMap, FromMip{ 1 }), Subresources(Builtin::Shadows::LinearShadowMaps, FromMip{ 1 }))
        .Build<DownsamplePass>();

    // After downsample, we need to render the "remainders" of the occluders (meshlets that were culled last frame, but shouldn't be this frame)
    // Using occluder meshlet culling command buffer, cull meshlets, but invert the bitfield and use occlusion culling
    graph->BuildComputePass("OcclusionMeshletRemaindersCullingPass")
        .WithShaderResource(Builtin::PerObjectBuffer, Builtin::PerMeshBuffer, Builtin::CameraBuffer)
        .WithUnorderedAccess(Builtin::MeshletCullingBitfieldGroup)
        .WithIndirectArguments(Builtin::IndirectCommandBuffers::MeshletCulling)
        .Build<MeshletCullingPass>(false, true, true);

    // Now, render the occluder remainders (prepass & shadows)
    if (drawShadows) {

        auto shadowOccluderRemainderPassBuilder = graph->BuildRenderPass("OccluderRemaindersShadowPass")
            .WithShaderResource(Builtin::PerObjectBuffer, 
                Builtin::NormalMatrixBuffer,
                Builtin::PerMeshBuffer, 
                Builtin::PerMeshInstanceBuffer,
                Builtin::PostSkinningVertices, 
                Builtin::CameraBuffer, 
                Builtin::Light::ViewResourceGroup,
                Builtin::Light::InfoBuffer,
                Builtin::Light::PointLightCubemapBuffer,
                Builtin::Light::DirectionalLightCascadeBuffer,
                Builtin::Light::SpotLightMatrixBuffer)
            .WithRenderTarget(Subresources(Builtin::Shadows::LinearShadowMaps, Mip{ 0, 1 }))
            .WithDepthReadWrite(Builtin::Shadows::ShadowMaps)
            .IsGeometryPass();

        if (meshShadersEnabled) {
            shadowOccluderRemainderPassBuilder.WithShaderResource(MESH_RESOURCE_IDFENTIFIERS, Builtin::MeshletCullingBitfieldGroup);
            //if (indirect) {
            shadowOccluderRemainderPassBuilder.WithIndirectArguments(Builtin::IndirectCommandBuffers::Opaque, Builtin::IndirectCommandBuffers::AlphaTest);
            //}
        }
        shadowOccluderRemainderPassBuilder.Build<ShadowPass>(wireframeEnabled, meshShadersEnabled, true, false);
    }

    auto occludersRemaindersPrepassBuilder = graph->BuildRenderPass("OccluderRemaindersPrepass") // Draws prepass for last frame's occluders
        .WithShaderResource(Builtin::PerObjectBuffer, 
            Builtin::PerMeshBuffer, 
            Builtin::PostSkinningVertices, 
            Builtin::CameraBuffer)
        .WithRenderTarget(
            Builtin::GBuffer::Normals,
			Builtin::GBuffer::MotionVectors,
            Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }), 
            Builtin::GBuffer::Albedo,
            Builtin::GBuffer::MetallicRoughness,
            Builtin::GBuffer::Emissive)
        .WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
        .IsGeometryPass();

    if (meshShadersEnabled) {
        occludersRemaindersPrepassBuilder.WithShaderResource(MESH_RESOURCE_IDFENTIFIERS, Builtin::PrimaryCamera::MeshletBitfield);
        //if (indirect) {
        occludersRemaindersPrepassBuilder.WithIndirectArguments(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque, Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest);
        //}
    }
    occludersRemaindersPrepassBuilder.Build<ZPrepass>(
        wireframeEnabled, 
        meshShadersEnabled, 
        true, 
        false);

    // After the remainders are rendered, we need to cull all meshlets that weren't marked as an occluder remainder. TODO: This duplicates culling work on non-visible meshlets
    //newGraph->BuildComputePass("OccludersMeshletCullingPass")
    //    .WithShaderResource(perObjectBuffer, perMeshBuffer, cameraBuffer)
    //    .WithUnorderedAccess(meshletCullingBitfieldBufferGroup)
    //    .WithIndirectArguments(meshletCullingCommandBufferResourceGroup)
    //    .Build<MeshletCullingPass>(true, false, false);
}

void BuildGeneralCullingPipeline(RenderGraph* graph) {

	bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	bool meshletCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshletCulling")();

    graph->BuildRenderPass("ClearOccludersIndirectDrawUAVsPass") // Clear command lists after occluders are drawn
        .WithCopyDest(Builtin::IndirectCommandBuffers::Opaque, Builtin::IndirectCommandBuffers::AlphaTest)
        .Build<ClearIndirectDrawCommandUAVsPass>(false);

    graph->BuildRenderPass("ClearMeshletCullingCommandUAVsPass1") // Clear meshlet culling reset command buffers from prepass
        .WithCopyDest(Builtin::IndirectCommandBuffers::MeshletCulling)
        .Build<ClearMeshletCullingCommandUAVsPass>();

    graph->BuildComputePass("ObjectCullingPass") // Performs frustrum and occlusion culling
        .WithShaderResource(Builtin::PerObjectBuffer, 
            Builtin::PerMeshBuffer, 
            Builtin::CameraBuffer, 
            Builtin::PrimaryCamera::LinearDepthMap, 
            Builtin::Shadows::LinearShadowMaps,
            Builtin::IndirectCommandBuffers::Master,
            Builtin::ActiveDrawSetIndices::Opaque,
            Builtin::ActiveDrawSetIndices::AlphaTest,
            Builtin::ActiveDrawSetIndices::Blend)
        .WithUnorderedAccess(Builtin::IndirectCommandBuffers::Opaque, 
            Builtin::IndirectCommandBuffers::AlphaTest, 
            Builtin::IndirectCommandBuffers::Blend,
            Builtin::IndirectCommandBuffers::MeshletCulling,
            Builtin::MeshInstanceMeshletCullingBitfieldGroup, 
            Builtin::MeshInstanceOcclusionCullingBitfieldGroup)
        .Build<ObjectCullingPass>(false, occlusionCulling);

    if (meshletCulling || occlusionCulling) {
        graph->BuildComputePass("MeshletFrustrumCullingPass") // Any meshes that are partially frustrum *or* occlusion culled are sent to the meshlet culling pass
            .WithShaderResource(Builtin::PerObjectBuffer, 
                Builtin::PerMeshBuffer, 
                Builtin::CameraBuffer, 
                Builtin::PrimaryCamera::LinearDepthMap, 
                Builtin::Shadows::LinearShadowMaps)
            .WithUnorderedAccess(Builtin::MeshletCullingBitfieldGroup)
            .WithIndirectArguments(Builtin::IndirectCommandBuffers::MeshletCulling)
            .Build<MeshletCullingPass>(false, false, true);
    }
}

void BuildZPrepass(RenderGraph* graph) {
    bool meshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
    bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	bool enableWireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();

    // Z prepass goes before light clustering for when active cluster determination is implemented
    auto newObjectsPrepassBuilder = graph->BuildRenderPass("newObjectsPrepass") // Do another prepass for any objects that aren't occluded
        .WithShaderResource(Builtin::PerObjectBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PostSkinningVertices,
            Builtin::CameraBuffer)
        .WithRenderTarget(
            Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }),
            Builtin::GBuffer::Normals,
            Builtin::GBuffer::MotionVectors);
    if (deferredRendering) {
        newObjectsPrepassBuilder.WithRenderTarget(
            Builtin::GBuffer::Albedo,
            Builtin::GBuffer::MetallicRoughness,
            Builtin::GBuffer::Emissive);
    }
    newObjectsPrepassBuilder.WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
        .IsGeometryPass();

    if (meshShadersEnabled) {
        newObjectsPrepassBuilder.WithShaderResource(MESH_RESOURCE_IDFENTIFIERS, Builtin::PrimaryCamera::MeshletBitfield);
        //if (indirect) {
        newObjectsPrepassBuilder.WithIndirectArguments(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque, Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest);
        //}
    }
    bool clearRTVs = false;
    if (!occlusionCulling || !indirect) {
        clearRTVs = true; // We will not run an earlier pass
    }
    newObjectsPrepassBuilder.Build<ZPrepass>(
        enableWireframe, 
        useMeshShaders,
        indirect, 
        clearRTVs);
}

void RegisterGTAOResources(RenderGraph* graph) {
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();

    TextureDescription workingDepthsDesc;
    workingDepthsDesc.arraySize = 1;
    workingDepthsDesc.channels = 1;
    workingDepthsDesc.isCubemap = false;
    workingDepthsDesc.hasRTV = false;
    workingDepthsDesc.hasUAV = true;
    workingDepthsDesc.format = DXGI_FORMAT_R32_FLOAT;
    workingDepthsDesc.generateMipMaps = true;
    ImageDimensions dims1 = { resolution.x, resolution.y, 0, 0 };
    workingDepthsDesc.imageDimensions.push_back(dims1);
    auto workingDepths = PixelBuffer::Create(workingDepthsDesc);
    workingDepths->SetName(L"GTAO Working Depths");

    TextureDescription workingEdgesDesc;
    workingEdgesDesc.arraySize = 1;
    workingEdgesDesc.channels = 1;
    workingEdgesDesc.isCubemap = false;
    workingEdgesDesc.hasRTV = false;
    workingEdgesDesc.hasUAV = true;
    workingEdgesDesc.format = DXGI_FORMAT_R8_UNORM;
    workingEdgesDesc.generateMipMaps = false;
    workingEdgesDesc.imageDimensions.push_back(dims1);
    auto workingEdges = PixelBuffer::Create(workingEdgesDesc);
    workingEdges->SetName(L"GTAO Working Edges");

    TextureDescription workingAOTermDesc;
    workingAOTermDesc.arraySize = 1;
    workingAOTermDesc.channels = 1;
    workingAOTermDesc.isCubemap = false;
    workingAOTermDesc.hasRTV = false;
    workingAOTermDesc.hasUAV = true;
    workingAOTermDesc.format = DXGI_FORMAT_R8_UINT;
    workingAOTermDesc.generateMipMaps = false;
    workingAOTermDesc.imageDimensions.push_back(dims1);
    auto workingAOTerm1 = PixelBuffer::Create(workingAOTermDesc);
    workingAOTerm1->SetName(L"GTAO Working AO Term 1");
    auto workingAOTerm2 = PixelBuffer::Create(workingAOTermDesc);
    workingAOTerm2->SetName(L"GTAO Working AO Term 2");
    std::shared_ptr<PixelBuffer> outputAO = PixelBuffer::Create(workingAOTermDesc);
    outputAO->SetName(L"GTAO Output AO Term");

    graph->RegisterResource(Builtin::GTAO::WorkingAOTerm1, workingAOTerm1);
    graph->RegisterResource(Builtin::GTAO::WorkingAOTerm2, workingAOTerm2);
    graph->RegisterResource(Builtin::GTAO::OutputAOTerm, outputAO);
    graph->RegisterResource(Builtin::GTAO::WorkingDepths, workingDepths);
    graph->RegisterResource(Builtin::GTAO::WorkingEdges, workingEdges);
}

void BuildGTAOPipeline(RenderGraph* graph, const Components::Camera* currentCamera) {
    auto GTAOConstantBuffer = ResourceManager::GetInstance().CreateIndexedConstantBuffer<GTAOInfo>(L"GTAO constants");
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();

    // Point-clamp sampler
    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    //samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

    auto samplerIndex = ResourceManager::GetInstance().CreateIndexedSampler(samplerDesc);

    GTAOInfo gtaoInfo;
    XeGTAO::GTAOSettings gtaoSettings;
    XeGTAO::GTAOConstants& gtaoConstants = gtaoInfo.g_GTAOConstants; // Intel's GTAO constants
    XeGTAO::GTAOUpdateConstants(gtaoConstants, resolution.x, resolution.y, gtaoSettings, false, 0, *currentCamera);
    // Bindless indices
    gtaoInfo.g_samplerPointClampDescriptorIndex = samplerIndex;

	auto workingDepths = graph->RequestResource<PixelBuffer>(Builtin::GTAO::WorkingDepths);
	auto workingEdges = graph->RequestResource<PixelBuffer>(Builtin::GTAO::WorkingEdges);
	auto workingAOTerm1 = graph->RequestResource<PixelBuffer>(Builtin::GTAO::WorkingAOTerm1);
	auto outputAO = graph->RequestResource<PixelBuffer>(Builtin::GTAO::OutputAOTerm);
	auto normalsWorldSpace = graph->RequestResource<PixelBuffer>(Builtin::GBuffer::Normals);

    // Filter pass
    gtaoInfo.g_srcRawDepthDescriptorIndex = graph->RequestResource<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture)->GetSRVInfo(0).index;
    gtaoInfo.g_outWorkingDepthMIP0DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(0).index;
    gtaoInfo.g_outWorkingDepthMIP1DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(1).index;
    gtaoInfo.g_outWorkingDepthMIP2DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(2).index;
    gtaoInfo.g_outWorkingDepthMIP3DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(3).index;
    gtaoInfo.g_outWorkingDepthMIP4DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(4).index;

    // Main pass
    gtaoInfo.g_srcWorkingDepthDescriptorIndex = workingDepths->GetSRVInfo(0).index;
    gtaoInfo.g_srcNormalmapDescriptorIndex = graph->RequestResource<PixelBuffer>(Builtin::GBuffer::Normals)->GetSRVInfo(0).index;
    // TODO: Hilbert lookup table
    gtaoInfo.g_outWorkingAOTermDescriptorIndex = workingAOTerm1->GetUAVShaderVisibleInfo(0).index;
    gtaoInfo.g_outWorkingEdgesDescriptorIndex = workingEdges->GetUAVShaderVisibleInfo(0).index;

    // Denoise pass
    gtaoInfo.g_srcWorkingEdgesDescriptorIndex = workingEdges->GetSRVInfo(0).index;
    gtaoInfo.g_outFinalAOTermDescriptorIndex = outputAO->GetUAVShaderVisibleInfo(0).index;

    UploadManager::GetInstance().UploadData(&gtaoInfo, sizeof(GTAOInfo), GTAOConstantBuffer.get(), 0);

    graph->BuildComputePass("GTAOFilterPass") // Depth filter pass
        .WithShaderResource(Builtin::GBuffer::Normals, Builtin::PrimaryCamera::DepthTexture)
        .WithUnorderedAccess(Builtin::GTAO::WorkingDepths)
        .Build<GTAOFilterPass>(GTAOConstantBuffer);

    graph->BuildComputePass("GTAOMainPass") // Main pass
        .WithShaderResource(Builtin::GBuffer::Normals, Builtin::GTAO::WorkingDepths)
        .WithUnorderedAccess(Builtin::GTAO::WorkingEdges, Builtin::GTAO::WorkingAOTerm1)
        .Build<GTAOMainPass>(GTAOConstantBuffer);

    graph->BuildComputePass("GTAODenoisePass") // Denoise pass
        .WithShaderResource(Builtin::GTAO::WorkingEdges, Builtin::GTAO::WorkingAOTerm1)
        .WithUnorderedAccess(Builtin::GTAO::OutputAOTerm)
        .Build<GTAODenoisePass>(GTAOConstantBuffer, workingAOTerm1->GetSRVInfo(0).index);
}

void BuildLightClusteringPipeline(RenderGraph* graph) {
    // light pages counter
    auto lightPagesCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int), false, true, false);
    lightPagesCounter->SetName(L"Light Pages Counter");
    graph->RegisterResource(Builtin::Light::PagesCounter, lightPagesCounter);

    graph->BuildComputePass("ClusterGenerationPass")
        .WithShaderResource(Builtin::CameraBuffer)
        .WithUnorderedAccess(Builtin::Light::ClusterBuffer)
        .Build<ClusterGenerationPass>();

    graph->BuildComputePass("LightCullingPass")
        .WithShaderResource(Builtin::CameraBuffer, Builtin::Light::BufferGroup)
        .WithUnorderedAccess(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer, Builtin::Light::PagesCounter)
        .Build<LightCullingPass>();
}

void BuildEnvironmentPipeline(RenderGraph* graph) {
    graph->BuildRenderPass("Environment Conversion Pass")
        .WithShaderResource(Builtin::Environment::WorkingHDRIGroup)
        .WithRenderTarget(Builtin::Environment::WorkingCubemapGroup)
        .Build<EnvironmentConversionPass>();

    graph->BuildComputePass("Environment Spherical Harmonics Pass")
        .WithShaderResource(Builtin::Environment::WorkingCubemapGroup)
        .WithUnorderedAccess(Builtin::Environment::InfoBuffer)
        .Build<EnvironmentSHPass>();

    graph->BuildRenderPass("Environment Prefilter Pass")
        .WithShaderResource(Builtin::Environment::WorkingCubemapGroup)
        .WithRenderTarget(Builtin::Environment::PrefilteredCubemapsGroup)
        .Build<EnvironmentFilterPass>();
}

void BuildMainShadowPass(RenderGraph* graph) {
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();

    bool clearRTVs = false;
    if (!occlusionCulling || !indirect) {
        clearRTVs = true; // We will not run an earlier pass
    }

    auto shadowBuilder = graph->BuildRenderPass("ShadowPass")
        .WithShaderResource(Builtin::PerObjectBuffer, 
            Builtin::NormalMatrixBuffer,
            Builtin::PerMeshBuffer, 
            Builtin::PerMeshInstanceBuffer,
            Builtin::PostSkinningVertices, 
            Builtin::CameraBuffer, 
            Builtin::Light::ViewResourceGroup,
            Builtin::Light::InfoBuffer,
            Builtin::Light::PointLightCubemapBuffer,
            Builtin::Light::DirectionalLightCascadeBuffer,
            Builtin::Light::SpotLightMatrixBuffer)
        .WithRenderTarget(Subresources(Builtin::Shadows::LinearShadowMaps, Mip{ 0, 1 }))
        .WithDepthReadWrite(Builtin::Shadows::ShadowMaps)
        .IsGeometryPass();

    if (useMeshShaders) {
        shadowBuilder.WithShaderResource(MESH_RESOURCE_IDFENTIFIERS, Builtin::MeshletCullingBitfieldGroup);
        if (indirect) {
            shadowBuilder.WithIndirectArguments(Builtin::IndirectCommandBuffers::Opaque, Builtin::IndirectCommandBuffers::AlphaTest, Builtin::IndirectCommandBuffers::Blend);
        }
    }
    shadowBuilder.Build<ShadowPass>(wireframe, useMeshShaders, indirect, clearRTVs);
}

void BuildPrimaryPass(RenderGraph* graph, Environment* currentEnvironment) {

	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();
	bool clusteredLighting = SettingsManager::GetInstance().getSettingGetter<bool>("enableClusteredLighting")();
	bool gtaoEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableGTAO")();
	bool meshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();

    std::string primaryPassName = deferredRendering ? "Deferred Pass" : "Forward Pass";
    auto primaryPassBuilder = graph->BuildRenderPass(primaryPassName)
        .WithShaderResource(Builtin::CameraBuffer, Builtin::Environment::PrefilteredCubemapsGroup)
        .WithRenderTarget(Builtin::Color::HDRColorTarget);

    if (!deferredRendering) {
        primaryPassBuilder.WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture);
        primaryPassBuilder.WithShaderResource(Builtin::PerObjectBuffer, Builtin::PerMeshBuffer, Builtin::PostSkinningVertices);
        primaryPassBuilder.IsGeometryPass();
    }
    else {
        primaryPassBuilder.WithDepthRead(Builtin::PrimaryCamera::DepthTexture);
    }

    if (clusteredLighting) {
        primaryPassBuilder.WithShaderResource(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer);
    }

    if (gtaoEnabled) {
        primaryPassBuilder.WithShaderResource(Builtin::GTAO::OutputAOTerm);
    }

    if (meshShaders && !deferredRendering) { // Don't need meshlets for deferred rendering
        primaryPassBuilder.WithShaderResource(MESH_RESOURCE_IDFENTIFIERS, Builtin::PrimaryCamera::MeshletBitfield);
        if (indirect) { // Indirect draws only supported with mesh shaders, becasue I'm not writing a separate codepath for doing it the bad way
            primaryPassBuilder.WithIndirectArguments(Builtin::IndirectCommandBuffers::Opaque, Builtin::IndirectCommandBuffers::AlphaTest);
        }
    }

    if (currentEnvironment != nullptr) {
        primaryPassBuilder.WithShaderResource(Builtin::Environment::CurrentCubemap);
    }

    if (deferredRendering) { // G-Buffer resources + depth
        primaryPassBuilder.WithShaderResource(
            Builtin::GBuffer::Normals,
            Builtin::GBuffer::Albedo,
            Builtin::GBuffer::MetallicRoughness,
            Builtin::PrimaryCamera::DepthTexture);
    }
    if (deferredRendering) {
        primaryPassBuilder.Build<DeferredRenderPass>();
    }
    else {
        primaryPassBuilder.Build<ForwardRenderPass>(
            wireframe, 
            meshShaders, 
            indirect, 
            gtaoEnabled ? graph->RequestResource<PixelBuffer>(Builtin::GTAO::OutputAOTerm)->GetSRVInfo(0).index : 0);
    }
}

void BuildPPLLPipeline(RenderGraph* graph) {
	bool drawShadows = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows")();
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool clusteredLighting = SettingsManager::GetInstance().getSettingGetter<bool>("enableClusteredLighting")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool gtao = SettingsManager::GetInstance().getSettingGetter<bool>("enableGTAO")();

    static const size_t aveFragsPerPixel = 12;
    auto numPPLLNodes = resolution.x * resolution.y * aveFragsPerPixel;
    static const size_t PPLLNodeSize = 24; // two uints, four floats
    TextureDescription desc;
    ImageDimensions dimensions;
    dimensions.width = resolution.x;
    dimensions.height = resolution.y;
    dimensions.rowPitch = resolution.x * sizeof(unsigned int);
    dimensions.slicePitch = dimensions.rowPitch * resolution.y;
    desc.imageDimensions.push_back(dimensions);
    desc.channels = 1;
    desc.format = DXGI_FORMAT_R32_UINT;
    desc.hasRTV = false;
    desc.hasUAV = true;
    desc.hasNonShaderVisibleUAV = true;
    auto PPLLHeadPointerTexture = PixelBuffer::Create(desc);
    PPLLHeadPointerTexture->SetName(L"PPLLHeadPointerTexture");
    auto PPLLBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(numPPLLNodes, PPLLNodeSize, false, true, false);
    PPLLBuffer->SetName(L"PPLLBuffer");
    auto PPLLCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int), false, true, false);
    PPLLCounter->SetName(L"PPLLCounter");

    graph->RegisterResource(Builtin::PPLL::HeadPointerTexture, PPLLHeadPointerTexture);
    graph->RegisterResource(Builtin::PPLL::Buffer, PPLLBuffer);
    graph->RegisterResource(Builtin::PPLL::Counter, PPLLCounter);

    auto PPLLFillBuilder = graph->BuildRenderPass("PPFillPass")
        .WithUnorderedAccess(Builtin::PPLL::HeadPointerTexture, Builtin::PPLL::Buffer, Builtin::PPLL::Counter)
        .WithShaderResource(Builtin::Light::BufferGroup, 
            Builtin::PostSkinningVertices, 
            Builtin::PerObjectBuffer, 
            Builtin::PerMeshBuffer, 
            Builtin::Environment::PrefilteredCubemapsGroup, 
            Builtin::Environment::InfoBuffer, 
            Builtin::CameraBuffer,  
            Builtin::GBuffer::Normals)
        .IsGeometryPass();
    if (gtao) {
        PPLLFillBuilder.WithShaderResource(Builtin::GTAO::OutputAOTerm);
    }
    if (drawShadows) {
        PPLLFillBuilder.WithShaderResource(Builtin::Shadows::ShadowMaps);
    }
    if (clusteredLighting) {
        PPLLFillBuilder.WithShaderResource(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer);
    }
    if (useMeshShaders) {
        PPLLFillBuilder.WithShaderResource(MESH_RESOURCE_IDFENTIFIERS);
        if (indirect) {
            PPLLFillBuilder.WithIndirectArguments(Builtin::IndirectCommandBuffers::Blend);
        }
    }
    PPLLFillBuilder.Build<PPLLFillPass>(wireframe,
        numPPLLNodes, 
        useMeshShaders, 
        indirect);

    graph->BuildRenderPass("PPLLResolvePass")
        .WithShaderResource(Builtin::PPLL::HeadPointerTexture, Builtin::PPLL::Buffer)
        .WithRenderTarget(Builtin::Color::HDRColorTarget)
        .Build<PPLLResolvePass>(PPLLHeadPointerTexture, PPLLBuffer);
}

void BuildBloomPipeline(RenderGraph* graph) {
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    // Calculate max mips
	unsigned int maxBloomMips = static_cast<unsigned int>(std::log2(std::max(resolution.x, resolution.y))) + 1;
    unsigned int numBloomMips = 5;
	if (maxBloomMips < numBloomMips) {
		numBloomMips = maxBloomMips; // Limit to max mips
	}

	// Downsample numBloomMips mips of the HDR color target
    for (unsigned int i = 0; i < numBloomMips; i++) {
        graph->BuildRenderPass("BloomDownsamplePass" + std::to_string(i))
            .WithShaderResource(Subresources(Builtin::Color::HDRColorTarget, Mip{ i, 1 }))
            .WithRenderTarget(Subresources(Builtin::Color::HDRColorTarget, Mip{ i + 1, 1 }))
			.Build<BloomSamplePass>(i, false);
    }

	// Upsample numBloomMips - 1 mips of the HDR color target, starting from the last mip
    for (unsigned int i = numBloomMips-1; i > 0; i--) {
        graph->BuildRenderPass("BloomUpsamplePass" + std::to_string(i))
            .WithShaderResource(Subresources(Builtin::Color::HDRColorTarget, Mip{ i + 1, 1 }))
            .WithRenderTarget(Subresources(Builtin::Color::HDRColorTarget, Mip{ i, 1 }))
            .Build<BloomSamplePass>(i, true);
    }
    
    // Upsample and blend the first mip with the HDR color target
	graph->BuildRenderPass("BloomUpsampleAndBlendPass")
		.WithShaderResource(Subresources(Builtin::Color::HDRColorTarget, Mip{ 1, 1 }))
		.WithUnorderedAccess(Subresources(Builtin::Color::HDRColorTarget, Mip{ 0, 1 }))
		.Build<BloomBlendPass>();
}