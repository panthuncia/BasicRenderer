#pragma once
#include "RenderGraphBuilder.h"
#include "Scene/Components.h"

void CreateGbufferResources(RenderGraphBuilder& builder) {
    // Gbuffer resources
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("screenResolution")();
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
    builder.RegisterResource(BuiltinResource::GBuf_Normals, normalsWorldSpace);

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
        builder.RegisterResource(BuiltinResource::GBuf_Albedo, albedo);

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
        builder.RegisterResource(BuiltinResource::GBuf_MetallicRoughness, metallicRoughness);

        TextureDescription emissiveDesc;
        emissiveDesc.arraySize = 1;
        emissiveDesc.channels = 4;
        emissiveDesc.isCubemap = false;
        emissiveDesc.hasRTV = true;
        emissiveDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        emissiveDesc.generateMipMaps = false;
        emissiveDesc.hasSRV = true;
        emissiveDesc.srvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        ImageDimensions emissiveDims = { resolution.x, resolution.y, 0, 0 };
        emissiveDesc.imageDimensions.push_back(emissiveDims);
        emissive = PixelBuffer::Create(emissiveDesc);
        emissive->SetName(L"Emissive");
        builder.RegisterResource(BuiltinResource::GBuf_Emissive, emissive);
    }
}

void BuildOcclusionCullingPipeline(RenderGraphBuilder& builder) {

	bool shadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows")();
	bool meshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool wireframeEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();

    builder.BuildRenderPass("ClearLastFrameIndirectDrawUAVsPass") // Clears indirect draws from last frame
        .WithCopyDest(BuiltinResource::PrimaryIndirectCommandBuffers)
        .Build<ClearIndirectDrawCommandUAVsPass>();

    builder.BuildRenderPass("ClearMeshletCullingCommandUAVsPass0") // Clear meshlet culling reset command buffers from last frame
        .WithCopyDest(BuiltinResource::MeshletCullingCommandBuffers)
        .Build<ClearMeshletCullingCommandUAVsPass>();

    builder.BuildComputePass("BuildOccluderDrawCommandsPass") // Builds draw command list for last frame's occluders
        .WithShaderResource(BuiltinResource::PerObjectBuffer, BuiltinResource::PerMeshBuffer, BuiltinResource::CameraBuffer)
        .WithUnorderedAccess(BuiltinResource::PrimaryIndirectCommandBuffers, 
            BuiltinResource::MeshletCullingCommandBuffers, 
            BuiltinResource::MeshInstanceMeshletCullingBitfieldGroup, 
            BuiltinResource::MeshInstanceOcclusionCullingBitfieldGroup)
        .Build<ObjectCullingPass>(true, true);

    // We need to draw occluder shadows early
    auto drawShadows = builder.RequestResource(BuiltinResource::ShadowMaps) != nullptr && shadowsEnabled;
    if (drawShadows) {
        auto shadowOccluderPassBuilder = builder.BuildRenderPass("OccluderShadowPrepass")
            .WithShaderResource(BuiltinResource::PerObjectBuffer, 
                BuiltinResource::PerMeshBuffer, 
                BuiltinResource::PostSkinningVertices, 
                BuiltinResource::CameraBuffer, 
                BuiltinResource::LightViewResourceGroup)
            .WithRenderTarget(Subresources(BuiltinResource::LinearShadowMaps, Mip{ 0, 1 }))
            .WithDepthReadWrite(BuiltinResource::ShadowMaps)
            .IsGeometryPass();

        if (meshShadersEnabled) {
            shadowOccluderPassBuilder.WithShaderResource(BuiltinResource::MeshResourceGroup, BuiltinResource::MeshletCullingBitfieldGroup);
            shadowOccluderPassBuilder.WithIndirectArguments(BuiltinResource::PrimaryIndirectCommandBuffers);
        }
        shadowOccluderPassBuilder.Build<ShadowPass>(wireframeEnabled, meshShadersEnabled, true, true);
    }

    auto occludersPrepassBuilder = builder.BuildRenderPass("OccludersPrepass") // Draws prepass for last frame's occluders
        .WithShaderResource(BuiltinResource::PerObjectBuffer,
            BuiltinResource::PerMeshBuffer,
            BuiltinResource::PostSkinningVertices,
            BuiltinResource::CameraBuffer)
        .WithRenderTarget(
            Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, Mip{ 0, 1 }),
            BuiltinResource::GBuf_Normals);
    if (deferredRendering) {
        occludersPrepassBuilder.WithRenderTarget(
            BuiltinResource::GBuf_Albedo,
            BuiltinResource::GBuf_MetallicRoughness,
            BuiltinResource::GBuf_Emissive);
    }

    occludersPrepassBuilder.WithDepthReadWrite(BuiltinResource::PrimaryCameraDepthTexture)
        .IsGeometryPass();

    if (meshShadersEnabled) {
        occludersPrepassBuilder.WithShaderResource(BuiltinResource::PerMeshBuffer, BuiltinResource::PrimaryCameraMeshletBitfield);
        //if (indirect) {
        occludersPrepassBuilder.WithIndirectArguments(BuiltinResource::PrimaryIndirectCommandBuffers);
        //}
    }
    occludersPrepassBuilder.Build<ZPrepass>(
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Normals, true), 
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Albedo, true),
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_MetallicRoughness, true),
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Emissive, true),
        wireframeEnabled, 
        meshShadersEnabled, 
        true, 
        true);

    // Single-pass downsample on all occluder-only depth maps
    // TODO: Unhandled edge case where HZB is not conservative when downsampling mips with non-even resolutions (bottom/side pixels get dropped)
    auto downsampleBuilder = builder.BuildComputePass("DownsamplePass")
        .WithShaderResource(Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, Mip{ 0, 1 }), Subresources(BuiltinResource::LinearShadowMaps, Mip{ 0, 1 }))
        .WithUnorderedAccess(Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, FromMip{ 1 }), Subresources(BuiltinResource::LinearShadowMaps, FromMip{ 1 }))
        .Build<DownsamplePass>();

    // After downsample, we need to render the "remainders" of the occluders (meshlets that were culled last frame, but shouldn't be this frame)
    // Using occluder meshlet culling command buffer, cull meshlets, but invert the bitfield and use occlusion culling
    builder.BuildComputePass("OcclusionMeshletRemaindersCullingPass")
        .WithShaderResource(BuiltinResource::PerObjectBuffer, BuiltinResource::PerMeshBuffer, BuiltinResource::CameraBuffer)
        .WithUnorderedAccess(BuiltinResource::MeshletCullingBitfieldGroup)
        .WithIndirectArguments(BuiltinResource::MeshletCullingCommandBuffers)
        .Build<MeshletCullingPass>(false, true, true);

    // Now, render the occluder remainders (prepass & shadows)
    if (drawShadows) {

        auto shadowOccluderRemainderPassBuilder = builder.BuildRenderPass("OccluderRemaindersShadowPass")
            .WithShaderResource(BuiltinResource::PerObjectBuffer, 
                BuiltinResource::PerMeshBuffer, 
                BuiltinResource::PostSkinningVertices, 
                BuiltinResource::CameraBuffer, 
                BuiltinResource::LightViewResourceGroup)
            .WithRenderTarget(Subresources(BuiltinResource::LinearShadowMaps, Mip{ 0, 1 }))
            .WithDepthReadWrite(BuiltinResource::ShadowMaps)
            .IsGeometryPass();

        if (meshShadersEnabled) {
            shadowOccluderRemainderPassBuilder.WithShaderResource(BuiltinResource::MeshResourceGroup, BuiltinResource::MeshletCullingBitfieldGroup);
            //if (indirect) {
            shadowOccluderRemainderPassBuilder.WithIndirectArguments(BuiltinResource::PrimaryIndirectCommandBuffers);
            //}
        }
        shadowOccluderRemainderPassBuilder.Build<ShadowPass>(wireframeEnabled, meshShadersEnabled, true, false);
    }

    auto occludersRemaindersPrepassBuilder = builder.BuildRenderPass("OccluderRemaindersPrepass") // Draws prepass for last frame's occluders
        .WithShaderResource(BuiltinResource::PerObjectBuffer, 
            BuiltinResource::PerMeshBuffer, 
            BuiltinResource::PostSkinningVertices, 
            BuiltinResource::CameraBuffer)
        .WithRenderTarget(
            builder.RequestResource(BuiltinResource::GBuf_Normals),
            Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, Mip{ 0, 1 }), 
            builder.RequestResource(BuiltinResource::GBuf_Albedo),
            builder.RequestResource(BuiltinResource::GBuf_MetallicRoughness),
            builder.RequestResource(BuiltinResource::GBuf_Emissive))
        .WithDepthReadWrite(BuiltinResource::PrimaryCameraDepthTexture)
        .IsGeometryPass();

    if (meshShadersEnabled) {
        occludersRemaindersPrepassBuilder.WithShaderResource(BuiltinResource::MeshResourceGroup, BuiltinResource::PrimaryCameraMeshletBitfield);
        //if (indirect) {
        occludersRemaindersPrepassBuilder.WithIndirectArguments(BuiltinResource::PrimaryIndirectCommandBuffers);
        //}
    }
    occludersRemaindersPrepassBuilder.Build<ZPrepass>(
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Normals),
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Albedo),
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_MetallicRoughness),
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Emissive),
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

void BuildGeneralCullingPipeline(RenderGraphBuilder& builder) {

	bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	bool meshletCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshletCulling")();

    builder.BuildRenderPass("ClearOccludersIndirectDrawUAVsPass") // Clear command lists after occluders are drawn
        .WithCopyDest(BuiltinResource::PrimaryIndirectCommandBuffers)
        .Build<ClearIndirectDrawCommandUAVsPass>();

    builder.BuildRenderPass("ClearMeshletCullingCommandUAVsPass1") // Clear meshlet culling reset command buffers from prepass
        .WithCopyDest(BuiltinResource::MeshletCullingCommandBuffers)
        .Build<ClearMeshletCullingCommandUAVsPass>();

    builder.BuildComputePass("ObjectCullingPass") // Performs frustrum and occlusion culling
        .WithShaderResource(BuiltinResource::PerObjectBuffer, 
            BuiltinResource::PerMeshBuffer, 
            BuiltinResource::CameraBuffer, 
            BuiltinResource::PrimaryCameraLinearDepthMap, 
            BuiltinResource::LinearShadowMaps)
        .WithUnorderedAccess(BuiltinResource::PrimaryIndirectCommandBuffers, 
            BuiltinResource::MeshletCullingCommandBuffers,
            BuiltinResource::MeshInstanceMeshletCullingBitfieldGroup, 
            BuiltinResource::MeshInstanceOcclusionCullingBitfieldGroup)
        .Build<ObjectCullingPass>(false, occlusionCulling);

    if (meshletCulling || occlusionCulling) {
        builder.BuildComputePass("MeshletFrustrumCullingPass") // Any meshes that are partially frustrum *or* occlusion culled are sent to the meshlet culling pass
            .WithShaderResource(BuiltinResource::PerObjectBuffer, 
                BuiltinResource::PerMeshBuffer, 
                BuiltinResource::CameraBuffer, 
                BuiltinResource::PrimaryCameraLinearDepthMap, 
                BuiltinResource::LinearShadowMaps)
            .WithUnorderedAccess(BuiltinResource::MeshletCullingBitfieldGroup)
            .WithIndirectArguments(BuiltinResource::MeshletCullingCommandBuffers)
            .Build<MeshletCullingPass>(false, false, true);
    }
}

void BuildZPrepass(RenderGraphBuilder& builder) {
    bool meshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
    bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	bool enableWireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();

    // Z prepass goes before light clustering for when active cluster determination is implemented
    auto newObjectsPrepassBuilder = builder.BuildRenderPass("newObjectsPrepass") // Do another prepass for any objects that aren't occluded
        .WithShaderResource(BuiltinResource::PerObjectBuffer,
            BuiltinResource::PerMeshBuffer,
            BuiltinResource::PostSkinningVertices,
            BuiltinResource::CameraBuffer)
        .WithRenderTarget(
            Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, Mip{ 0, 1 }),
            BuiltinResource::GBuf_Normals);
    if (deferredRendering) {
        newObjectsPrepassBuilder.WithRenderTarget(
            BuiltinResource::GBuf_Albedo,
            BuiltinResource::GBuf_MetallicRoughness,
            BuiltinResource::GBuf_Emissive);
    }
    newObjectsPrepassBuilder.WithDepthReadWrite(BuiltinResource::PrimaryCameraDepthTexture)
        .IsGeometryPass();

    if (meshShadersEnabled) {
        newObjectsPrepassBuilder.WithShaderResource(BuiltinResource::MeshResourceGroup, BuiltinResource::PrimaryCameraMeshletBitfield);
        //if (indirect) {
        newObjectsPrepassBuilder.WithIndirectArguments(BuiltinResource::PrimaryIndirectCommandBuffers);
        //}
    }
    bool clearRTVs = false;
    if (!occlusionCulling || !indirect) {
        clearRTVs = true; // We will not run an earlier pass
    }
    newObjectsPrepassBuilder.Build<ZPrepass>(
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Normals, true),
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Albedo, true),
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_MetallicRoughness, true),
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Emissive, true),
        enableWireframe, 
        useMeshShaders,
        indirect, 
        clearRTVs);
}

void RegisterGTAOResources(RenderGraphBuilder& builder) {
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("screenResolution")();

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

    builder.RegisterResource(BuiltinResource::GTAO_WorkingAOTerm1, workingAOTerm1);
    builder.RegisterResource(BuiltinResource::GTAO_WorkingAOTerm2, workingAOTerm2);
    builder.RegisterResource(BuiltinResource::GTAO_OutputAOTerm, outputAO);
    builder.RegisterResource(BuiltinResource::GTAO_WorkingDepths, workingDepths);
    builder.RegisterResource(BuiltinResource::GTAO_WorkingEdges, workingEdges);
}

void BuildGTAOPipeline(RenderGraphBuilder& builder, const Components::Camera* currentCamera) {
    auto GTAOConstantBuffer = ResourceManager::GetInstance().CreateIndexedConstantBuffer<GTAOInfo>(L"GTAO constants");
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("screenResolution")();

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

	auto workingDepths = builder.RequestGloballyIndexedResource(BuiltinResource::GTAO_WorkingDepths);
	auto workingEdges = builder.RequestGloballyIndexedResource(BuiltinResource::GTAO_WorkingEdges);
	auto workingAOTerm1 = builder.RequestGloballyIndexedResource(BuiltinResource::GTAO_WorkingAOTerm1);
	auto outputAO = builder.RequestGloballyIndexedResource(BuiltinResource::GTAO_OutputAOTerm);
	auto normalsWorldSpace = builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Normals);

    // Filter pass
    gtaoInfo.g_srcRawDepthDescriptorIndex = builder.RequestGloballyIndexedResource(BuiltinResource::PrimaryCameraDepthTexture)->GetSRVInfo(0).index;
    gtaoInfo.g_outWorkingDepthMIP0DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(0).index;
    gtaoInfo.g_outWorkingDepthMIP1DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(1).index;
    gtaoInfo.g_outWorkingDepthMIP2DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(2).index;
    gtaoInfo.g_outWorkingDepthMIP3DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(3).index;
    gtaoInfo.g_outWorkingDepthMIP4DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(4).index;

    // Main pass
    gtaoInfo.g_srcWorkingDepthDescriptorIndex = workingDepths->GetSRVInfo(0).index;
    gtaoInfo.g_srcNormalmapDescriptorIndex = builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Normals)->GetSRVInfo(0).index;
    // TODO: Hilbert lookup table
    gtaoInfo.g_outWorkingAOTermDescriptorIndex = workingAOTerm1->GetUAVShaderVisibleInfo(0).index;
    gtaoInfo.g_outWorkingEdgesDescriptorIndex = workingEdges->GetUAVShaderVisibleInfo(0).index;

    // Denoise pass
    gtaoInfo.g_srcWorkingEdgesDescriptorIndex = workingEdges->GetSRVInfo(0).index;
    gtaoInfo.g_outFinalAOTermDescriptorIndex = outputAO->GetUAVShaderVisibleInfo(0).index;

    UploadManager::GetInstance().UploadData(&gtaoInfo, sizeof(GTAOInfo), GTAOConstantBuffer.get(), 0);

    builder.BuildComputePass("GTAOFilterPass") // Depth filter pass
        .WithShaderResource(BuiltinResource::GBuf_Normals, BuiltinResource::PrimaryCameraDepthTexture)
        .WithUnorderedAccess(workingDepths)
        .Build<GTAOFilterPass>(GTAOConstantBuffer);

    builder.BuildComputePass("GTAOMainPass") // Main pass
        .WithShaderResource(normalsWorldSpace, workingDepths)
        .WithUnorderedAccess(workingEdges, workingAOTerm1)
        .Build<GTAOMainPass>(GTAOConstantBuffer);

    builder.BuildComputePass("GTAODenoisePass") // Denoise pass
        .WithShaderResource(workingEdges, workingAOTerm1)
        .WithUnorderedAccess(outputAO)
        .Build<GTAODenoisePass>(GTAOConstantBuffer, workingAOTerm1->GetSRVInfo(0).index);
}

void BuildLightClusteringPipeline(RenderGraphBuilder& builder) {
    // light pages counter
    auto lightPagesCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int), false, true, false);
    lightPagesCounter->SetName(L"Light Pages Counter");
    builder.RegisterResource(BuiltinResource::LightPagesCounter, lightPagesCounter);

    builder.BuildComputePass("ClusterGenerationPass")
        .WithShaderResource(BuiltinResource::CameraBuffer)
        .WithUnorderedAccess(BuiltinResource::LightClusterBuffer)
        .Build<ClusterGenerationPass>(builder.RequestGloballyIndexedResource(BuiltinResource::LightClusterBuffer));

    builder.BuildComputePass("LightCullingPass")
        .WithShaderResource(BuiltinResource::CameraBuffer, BuiltinResource::LightBufferGroup)
        .WithUnorderedAccess(BuiltinResource::LightClusterBuffer, BuiltinResource::LightPagesBuffer, lightPagesCounter)
        .Build<LightCullingPass>(
            builder.RequestGloballyIndexedResource(BuiltinResource::LightClusterBuffer), 
            builder.RequestGloballyIndexedResource(BuiltinResource::LightPagesBuffer), lightPagesCounter);
}

void BuildEnvironmentPipeline(RenderGraphBuilder& builder) {
    builder.BuildRenderPass("Environment Conversion Pass")
        .WithShaderResource(BuiltinResource::WorkingEnvironmentHDRIGroup)
        .WithRenderTarget(BuiltinResource::WorkingEnvironmentCubemapGroup)
        .Build<EnvironmentConversionPass>();

    builder.BuildComputePass("Environment Spherical Harmonics Pass")
        .WithShaderResource(BuiltinResource::WorkingEnvironmentCubemapGroup)
        .WithUnorderedAccess(BuiltinResource::EnvironmentsInfoBuffer)
        .Build<EnvironmentSHPass>();

    builder.BuildRenderPass("Environment Prefilter Pass")
        .WithShaderResource(BuiltinResource::WorkingEnvironmentCubemapGroup)
        .WithRenderTarget(BuiltinResource::EnvironmentPrefilteredCubemapsGroup)
        .Build<EnvironmentFilterPass>();
}

void BuildMainShadowPass(RenderGraphBuilder& builder) {
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();

    bool clearRTVs = false;
    if (!occlusionCulling || !indirect) {
        clearRTVs = true; // We will not run an earlier pass
    }

    auto shadowBuilder = builder.BuildRenderPass("ShadowPass")
        .WithShaderResource(BuiltinResource::PerObjectBuffer, 
            BuiltinResource::PerMeshBuffer, 
            BuiltinResource::PostSkinningVertices, 
            BuiltinResource::CameraBuffer, 
            BuiltinResource::LightViewResourceGroup)
        .WithRenderTarget(Subresources(BuiltinResource::LinearShadowMaps, Mip{ 0, 1 }))
        .WithDepthReadWrite(BuiltinResource::ShadowMaps)
        .IsGeometryPass();

    if (useMeshShaders) {
        shadowBuilder.WithShaderResource(BuiltinResource::MeshResourceGroup, BuiltinResource::MeshletCullingBitfieldGroup);
        if (indirect) {
            shadowBuilder.WithIndirectArguments(BuiltinResource::PrimaryIndirectCommandBuffers);
        }
    }
    shadowBuilder.Build<ShadowPass>(wireframe, useMeshShaders, indirect, clearRTVs);
}

void BuildPrimaryPass(RenderGraphBuilder& builder, Environment* currentEnvironment) {

	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();
	bool clusteredLighting = SettingsManager::GetInstance().getSettingGetter<bool>("enableClusteredLighting")();
	bool gtaoEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableGTAO")();
	bool meshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();

    std::string primaryPassName = deferredRendering ? "Deferred Pass" : "Forward Pass";
    auto primaryPassBuilder = builder.BuildRenderPass(primaryPassName)
        .WithShaderResource(BuiltinResource::CameraBuffer, BuiltinResource::EnvironmentPrefilteredCubemapsGroup)
        .WithRenderTarget(BuiltinResource::HDRColorTarget);

    if (!deferredRendering) {
        primaryPassBuilder.WithDepthReadWrite(BuiltinResource::PrimaryCameraDepthTexture);
        primaryPassBuilder.WithShaderResource(BuiltinResource::PerObjectBuffer, BuiltinResource::PerMeshBuffer, BuiltinResource::PostSkinningVertices);
        primaryPassBuilder.IsGeometryPass();
    }
    else {
        primaryPassBuilder.WithDepthRead(BuiltinResource::PrimaryCameraDepthTexture);
    }

    if (clusteredLighting) {
        primaryPassBuilder.WithShaderResource(BuiltinResource::LightClusterBuffer, BuiltinResource::LightPagesBuffer);
    }

    if (gtaoEnabled) {
        primaryPassBuilder.WithShaderResource(BuiltinResource::GTAO_OutputAOTerm);
    }

    if (meshShaders && !deferredRendering) { // Don't need meshlets for deferred rendering
        primaryPassBuilder.WithShaderResource(BuiltinResource::MeshResourceGroup, BuiltinResource::PrimaryCameraMeshletBitfield);
        if (indirect) { // Indirect draws only supported with mesh shaders, becasue I'm not writing a separate codepath for doing it the bad way
            primaryPassBuilder.WithIndirectArguments(BuiltinResource::PrimaryIndirectCommandBuffers);
        }
    }

    if (currentEnvironment != nullptr) {
        builder.RegisterResource(BuiltinResource::CurrentEnvironmentCubemap, currentEnvironment->GetEnvironmentCubemap());
        primaryPassBuilder.WithShaderResource(currentEnvironment->GetEnvironmentCubemap());
    }

    if (deferredRendering) { // G-Buffer resources + depth
        primaryPassBuilder.WithShaderResource(
            builder.RequestResource(BuiltinResource::GBuf_Normals),
            builder.RequestResource(BuiltinResource::GBuf_Albedo),
            builder.RequestResource(BuiltinResource::GBuf_MetallicRoughness),
            builder.RequestResource(BuiltinResource::PrimaryCameraDepthTexture));
    }
    if (deferredRendering) {
        primaryPassBuilder.Build<DeferredRenderPass>(
            gtaoEnabled ? builder.RequestGloballyIndexedResource(BuiltinResource::GTAO_OutputAOTerm)->GetSRVInfo(0).index : 0, 
            builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Normals)->GetSRVInfo(0).index, 
            builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Albedo)->GetSRVInfo(0).index, 
            builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_MetallicRoughness)->GetSRVInfo(0).index, 
            builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Emissive)->GetSRVInfo(0).index, 
            builder.RequestGloballyIndexedResource(BuiltinResource::PrimaryCameraDepthTexture)->GetSRVInfo(0).index);
    }
    else {
        primaryPassBuilder.Build<ForwardRenderPass>(
            wireframe, 
            meshShaders, 
            indirect, 
            gtaoEnabled ? builder.RequestGloballyIndexedResource(BuiltinResource::GTAO_OutputAOTerm)->GetSRVInfo(0).index : 0);
    }
}

void BuildPPLLPipeline(RenderGraphBuilder& builder) {
	bool drawShadows = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows")();
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("screenResolution")();
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

    builder.RegisterResource(BuiltinResource::PPLLHeadPointerTexture, PPLLHeadPointerTexture);
    builder.RegisterResource(BuiltinResource::PPLLBuffer, PPLLBuffer);
    builder.RegisterResource(BuiltinResource::PPLLCounter, PPLLCounter);

    auto PPLLFillBuilder = builder.BuildRenderPass("PPFillPass")
        .WithUnorderedAccess(PPLLHeadPointerTexture, PPLLBuffer, PPLLCounter)
        .WithShaderResource(BuiltinResource::LightBufferGroup, 
            BuiltinResource::PostSkinningVertices, 
            BuiltinResource::PerObjectBuffer, 
            BuiltinResource::PerMeshBuffer, 
            BuiltinResource::EnvironmentPrefilteredCubemapsGroup, 
            BuiltinResource::EnvironmentsInfoBuffer, 
            BuiltinResource::CameraBuffer,  
            BuiltinResource::GBuf_Normals)
        .IsGeometryPass();
    if (gtao) {
        PPLLFillBuilder.WithShaderResource(BuiltinResource::GTAO_OutputAOTerm);
    }
    if (drawShadows) {
        PPLLFillBuilder.WithShaderResource(BuiltinResource::ShadowMaps);
    }
    if (clusteredLighting) {
        PPLLFillBuilder.WithShaderResource(BuiltinResource::LightClusterBuffer, BuiltinResource::LightPagesBuffer);
    }
    if (useMeshShaders) {
        PPLLFillBuilder.WithShaderResource(BuiltinResource::MeshResourceGroup);
        if (indirect) {
            PPLLFillBuilder.WithIndirectArguments(BuiltinResource::PrimaryIndirectCommandBuffers);
        }
    }
    PPLLFillBuilder.Build<PPLLFillPass>(wireframe, 
        PPLLHeadPointerTexture, 
        PPLLBuffer, 
        PPLLCounter, 
        numPPLLNodes, 
        useMeshShaders, 
        indirect, 
        gtao ? builder.RequestGloballyIndexedResource(BuiltinResource::GTAO_OutputAOTerm)->GetSRVInfo(0).index : 0,
        builder.RequestGloballyIndexedResource(BuiltinResource::GBuf_Normals)->GetSRVInfo(0).index);


    builder.BuildRenderPass("PPLLResolvePass")
        .WithShaderResource(PPLLHeadPointerTexture, PPLLBuffer)
        .WithRenderTarget(BuiltinResource::HDRColorTarget)
        .Build<PPLLResolvePass>(PPLLHeadPointerTexture, PPLLBuffer);
}
