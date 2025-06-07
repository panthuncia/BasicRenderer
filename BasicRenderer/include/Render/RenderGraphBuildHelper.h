#pragma once
#include "Scene/Components.h"
#include "Render/RenderGraph.h"

void CreateGbufferResources(RenderGraph* graph) {
    // Gbuffer resources
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
    graph->RegisterResource(BuiltinResource::GBuf_Normals, normalsWorldSpace);

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
        graph->RegisterResource(BuiltinResource::GBuf_Albedo, albedo);

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
        graph->RegisterResource(BuiltinResource::GBuf_MetallicRoughness, metallicRoughness);

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
        graph->RegisterResource(BuiltinResource::GBuf_Emissive, emissive);
    }
}

void BuildOcclusionCullingPipeline(RenderGraph* graph) {

	bool shadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows")();
	bool meshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool wireframeEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();

    graph->BuildRenderPass("ClearLastFrameIndirectDrawUAVsPass") // Clears indirect draws from last frame
        .WithCopyDest(BuiltinResource::PrimaryIndirectCommandBuffers)
        .Build<ClearIndirectDrawCommandUAVsPass>();

    graph->BuildRenderPass("ClearMeshletCullingCommandUAVsPass0") // Clear meshlet culling reset command buffers from last frame
        .WithCopyDest(BuiltinResource::MeshletCullingCommandBuffers)
        .Build<ClearMeshletCullingCommandUAVsPass>();

    graph->BuildComputePass("BuildOccluderDrawCommandsPass") // Builds draw command list for last frame's occluders
        .WithShaderResource(BuiltinResource::PerObjectBuffer, BuiltinResource::PerMeshBuffer, BuiltinResource::CameraBuffer)
        .WithUnorderedAccess(BuiltinResource::PrimaryIndirectCommandBuffers, 
            BuiltinResource::MeshletCullingCommandBuffers, 
            BuiltinResource::MeshInstanceMeshletCullingBitfieldGroup, 
            BuiltinResource::MeshInstanceOcclusionCullingBitfieldGroup)
        .Build<ObjectCullingPass>(true, true);

    // We need to draw occluder shadows early
    auto drawShadows = graph->RequestResource(BuiltinResource::ShadowMaps) != nullptr && shadowsEnabled;
    if (drawShadows) {
        auto shadowOccluderPassBuilder = graph->BuildRenderPass("OccluderShadowPrepass")
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

    auto occludersPrepassBuilder = graph->BuildRenderPass("OccludersPrepass") // Draws prepass for last frame's occluders
        .WithShaderResource(BuiltinResource::PerObjectBuffer,
            BuiltinResource::PerMeshBuffer,
            BuiltinResource::PostSkinningVertices,
            BuiltinResource::CameraBuffer)
        .WithRenderTarget(
            Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, Mip{ 0, 1 }),
            BuiltinResource::GBuf_Normals,
            BuiltinResource::GBuf_MotionVectors);
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
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Normals, true), 
		graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_MotionVectors, true),
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Albedo, true),
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_MetallicRoughness, true),
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Emissive, true),
        wireframeEnabled, 
        meshShadersEnabled, 
        true, 
        true);

    // Single-pass downsample on all occluder-only depth maps
    // TODO: Unhandled edge case where HZB is not conservative when downsampling mips with non-even resolutions (bottom/side pixels get dropped)
    auto downsampleBuilder = graph->BuildComputePass("DownsamplePass")
        .WithShaderResource(Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, Mip{ 0, 1 }), Subresources(BuiltinResource::LinearShadowMaps, Mip{ 0, 1 }))
        .WithUnorderedAccess(Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, FromMip{ 1 }), Subresources(BuiltinResource::LinearShadowMaps, FromMip{ 1 }))
        .Build<DownsamplePass>();

    // After downsample, we need to render the "remainders" of the occluders (meshlets that were culled last frame, but shouldn't be this frame)
    // Using occluder meshlet culling command buffer, cull meshlets, but invert the bitfield and use occlusion culling
    graph->BuildComputePass("OcclusionMeshletRemaindersCullingPass")
        .WithShaderResource(BuiltinResource::PerObjectBuffer, BuiltinResource::PerMeshBuffer, BuiltinResource::CameraBuffer)
        .WithUnorderedAccess(BuiltinResource::MeshletCullingBitfieldGroup)
        .WithIndirectArguments(BuiltinResource::MeshletCullingCommandBuffers)
        .Build<MeshletCullingPass>(false, true, true);

    // Now, render the occluder remainders (prepass & shadows)
    if (drawShadows) {

        auto shadowOccluderRemainderPassBuilder = graph->BuildRenderPass("OccluderRemaindersShadowPass")
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

    auto occludersRemaindersPrepassBuilder = graph->BuildRenderPass("OccluderRemaindersPrepass") // Draws prepass for last frame's occluders
        .WithShaderResource(BuiltinResource::PerObjectBuffer, 
            BuiltinResource::PerMeshBuffer, 
            BuiltinResource::PostSkinningVertices, 
            BuiltinResource::CameraBuffer)
        .WithRenderTarget(
            BuiltinResource::GBuf_Normals,
			BuiltinResource::GBuf_MotionVectors,
            Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, Mip{ 0, 1 }), 
            BuiltinResource::GBuf_Albedo,
            BuiltinResource::GBuf_MetallicRoughness,
            BuiltinResource::GBuf_Emissive)
        .WithDepthReadWrite(BuiltinResource::PrimaryCameraDepthTexture)
        .IsGeometryPass();

    if (meshShadersEnabled) {
        occludersRemaindersPrepassBuilder.WithShaderResource(BuiltinResource::MeshResourceGroup, BuiltinResource::PrimaryCameraMeshletBitfield);
        //if (indirect) {
        occludersRemaindersPrepassBuilder.WithIndirectArguments(BuiltinResource::PrimaryIndirectCommandBuffers);
        //}
    }
    occludersRemaindersPrepassBuilder.Build<ZPrepass>(
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Normals),
		graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_MotionVectors),
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Albedo),
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_MetallicRoughness),
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Emissive),
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
        .WithCopyDest(BuiltinResource::PrimaryIndirectCommandBuffers)
        .Build<ClearIndirectDrawCommandUAVsPass>();

    graph->BuildRenderPass("ClearMeshletCullingCommandUAVsPass1") // Clear meshlet culling reset command buffers from prepass
        .WithCopyDest(BuiltinResource::MeshletCullingCommandBuffers)
        .Build<ClearMeshletCullingCommandUAVsPass>();

    graph->BuildComputePass("ObjectCullingPass") // Performs frustrum and occlusion culling
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
        graph->BuildComputePass("MeshletFrustrumCullingPass") // Any meshes that are partially frustrum *or* occlusion culled are sent to the meshlet culling pass
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

void BuildZPrepass(RenderGraph* graph) {
    bool meshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
    bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	bool enableWireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();

    // Z prepass goes before light clustering for when active cluster determination is implemented
    auto newObjectsPrepassBuilder = graph->BuildRenderPass("newObjectsPrepass") // Do another prepass for any objects that aren't occluded
        .WithShaderResource(BuiltinResource::PerObjectBuffer,
            BuiltinResource::PerMeshBuffer,
            BuiltinResource::PostSkinningVertices,
            BuiltinResource::CameraBuffer)
        .WithRenderTarget(
            Subresources(BuiltinResource::PrimaryCameraLinearDepthMap, Mip{ 0, 1 }),
            BuiltinResource::GBuf_Normals,
            BuiltinResource::GBuf_MotionVectors);
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
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Normals, true),
		graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_MotionVectors, true),
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Albedo, true),
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_MetallicRoughness, true),
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Emissive, true),
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

    graph->RegisterResource(BuiltinResource::GTAO_WorkingAOTerm1, workingAOTerm1);
    graph->RegisterResource(BuiltinResource::GTAO_WorkingAOTerm2, workingAOTerm2);
    graph->RegisterResource(BuiltinResource::GTAO_OutputAOTerm, outputAO);
    graph->RegisterResource(BuiltinResource::GTAO_WorkingDepths, workingDepths);
    graph->RegisterResource(BuiltinResource::GTAO_WorkingEdges, workingEdges);
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

	auto workingDepths = graph->RequestResource<PixelBuffer>(BuiltinResource::GTAO_WorkingDepths);
	auto workingEdges = graph->RequestResource<PixelBuffer>(BuiltinResource::GTAO_WorkingEdges);
	auto workingAOTerm1 = graph->RequestResource<PixelBuffer>(BuiltinResource::GTAO_WorkingAOTerm1);
	auto outputAO = graph->RequestResource<PixelBuffer>(BuiltinResource::GTAO_OutputAOTerm);
	auto normalsWorldSpace = graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Normals);

    // Filter pass
    gtaoInfo.g_srcRawDepthDescriptorIndex = graph->RequestResource<PixelBuffer>(BuiltinResource::PrimaryCameraDepthTexture)->GetSRVInfo(0).index;
    gtaoInfo.g_outWorkingDepthMIP0DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(0).index;
    gtaoInfo.g_outWorkingDepthMIP1DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(1).index;
    gtaoInfo.g_outWorkingDepthMIP2DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(2).index;
    gtaoInfo.g_outWorkingDepthMIP3DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(3).index;
    gtaoInfo.g_outWorkingDepthMIP4DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(4).index;

    // Main pass
    gtaoInfo.g_srcWorkingDepthDescriptorIndex = workingDepths->GetSRVInfo(0).index;
    gtaoInfo.g_srcNormalmapDescriptorIndex = graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Normals)->GetSRVInfo(0).index;
    // TODO: Hilbert lookup table
    gtaoInfo.g_outWorkingAOTermDescriptorIndex = workingAOTerm1->GetUAVShaderVisibleInfo(0).index;
    gtaoInfo.g_outWorkingEdgesDescriptorIndex = workingEdges->GetUAVShaderVisibleInfo(0).index;

    // Denoise pass
    gtaoInfo.g_srcWorkingEdgesDescriptorIndex = workingEdges->GetSRVInfo(0).index;
    gtaoInfo.g_outFinalAOTermDescriptorIndex = outputAO->GetUAVShaderVisibleInfo(0).index;

    UploadManager::GetInstance().UploadData(&gtaoInfo, sizeof(GTAOInfo), GTAOConstantBuffer.get(), 0);

    graph->BuildComputePass("GTAOFilterPass") // Depth filter pass
        .WithShaderResource(BuiltinResource::GBuf_Normals, BuiltinResource::PrimaryCameraDepthTexture)
        .WithUnorderedAccess(BuiltinResource::GTAO_WorkingDepths, BuiltinResource::GTAO_WorkingDepths)
        .Build<GTAOFilterPass>(GTAOConstantBuffer);

    graph->BuildComputePass("GTAOMainPass") // Main pass
        .WithShaderResource(BuiltinResource::GBuf_Normals, BuiltinResource::GTAO_WorkingDepths)
        .WithUnorderedAccess(BuiltinResource::GTAO_WorkingEdges, BuiltinResource::GTAO_WorkingAOTerm1)
        .Build<GTAOMainPass>(GTAOConstantBuffer);

    graph->BuildComputePass("GTAODenoisePass") // Denoise pass
        .WithShaderResource(BuiltinResource::GTAO_WorkingEdges, BuiltinResource::GTAO_WorkingAOTerm1)
        .WithUnorderedAccess(BuiltinResource::GTAO_OutputAOTerm)
        .Build<GTAODenoisePass>(GTAOConstantBuffer, workingAOTerm1->GetSRVInfo(0).index);
}

void BuildLightClusteringPipeline(RenderGraph* graph) {
    // light pages counter
    auto lightPagesCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int), false, true, false);
    lightPagesCounter->SetName(L"Light Pages Counter");
    graph->RegisterResource(BuiltinResource::LightPagesCounter, lightPagesCounter);

    graph->BuildComputePass("ClusterGenerationPass")
        .WithShaderResource(BuiltinResource::CameraBuffer)
        .WithUnorderedAccess(BuiltinResource::LightClusterBuffer)
        .Build<ClusterGenerationPass>(graph->RequestResource<GloballyIndexedResource>(BuiltinResource::LightClusterBuffer));

    graph->BuildComputePass("LightCullingPass")
        .WithShaderResource(BuiltinResource::CameraBuffer, BuiltinResource::LightBufferGroup)
        .WithUnorderedAccess(BuiltinResource::LightClusterBuffer, BuiltinResource::LightPagesBuffer, BuiltinResource::LightPagesCounter)
        .Build<LightCullingPass>(
            graph->RequestResource<GloballyIndexedResource>(BuiltinResource::LightClusterBuffer), 
            graph->RequestResource<GloballyIndexedResource>(BuiltinResource::LightPagesBuffer), 
            graph->RequestResource<GloballyIndexedResource>(BuiltinResource::LightPagesCounter));
}

void BuildEnvironmentPipeline(RenderGraph* graph) {
    graph->BuildRenderPass("Environment Conversion Pass")
        .WithShaderResource(BuiltinResource::WorkingEnvironmentHDRIGroup)
        .WithRenderTarget(BuiltinResource::WorkingEnvironmentCubemapGroup)
        .Build<EnvironmentConversionPass>();

    graph->BuildComputePass("Environment Spherical Harmonics Pass")
        .WithShaderResource(BuiltinResource::WorkingEnvironmentCubemapGroup)
        .WithUnorderedAccess(BuiltinResource::EnvironmentsInfoBuffer)
        .Build<EnvironmentSHPass>();

    graph->BuildRenderPass("Environment Prefilter Pass")
        .WithShaderResource(BuiltinResource::WorkingEnvironmentCubemapGroup)
        .WithRenderTarget(BuiltinResource::EnvironmentPrefilteredCubemapsGroup)
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

void BuildPrimaryPass(RenderGraph* graph, Environment* currentEnvironment) {

	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();
	bool clusteredLighting = SettingsManager::GetInstance().getSettingGetter<bool>("enableClusteredLighting")();
	bool gtaoEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableGTAO")();
	bool meshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();

    std::string primaryPassName = deferredRendering ? "Deferred Pass" : "Forward Pass";
    auto primaryPassBuilder = graph->BuildRenderPass(primaryPassName)
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
        primaryPassBuilder.WithShaderResource(BuiltinResource::CurrentEnvironmentCubemap);
    }

    if (deferredRendering) { // G-Buffer resources + depth
        primaryPassBuilder.WithShaderResource(
            BuiltinResource::GBuf_Normals,
            BuiltinResource::GBuf_Albedo,
            BuiltinResource::GBuf_MetallicRoughness,
            BuiltinResource::PrimaryCameraDepthTexture);
    }
    if (deferredRendering) {
        primaryPassBuilder.Build<DeferredRenderPass>(
            gtaoEnabled ? graph->RequestResource<PixelBuffer>(BuiltinResource::GTAO_OutputAOTerm)->GetSRVInfo(0).index : 0, 
            graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Normals)->GetSRVInfo(0).index, 
            graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Albedo)->GetSRVInfo(0).index, 
            graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_MetallicRoughness)->GetSRVInfo(0).index, 
            graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Emissive)->GetSRVInfo(0).index, 
            graph->RequestResource<PixelBuffer>(BuiltinResource::PrimaryCameraDepthTexture)->GetSRVInfo(0).index);
    }
    else {
        primaryPassBuilder.Build<ForwardRenderPass>(
            wireframe, 
            meshShaders, 
            indirect, 
            gtaoEnabled ? graph->RequestResource<PixelBuffer>(BuiltinResource::GTAO_OutputAOTerm)->GetSRVInfo(0).index : 0);
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

    graph->RegisterResource(BuiltinResource::PPLLHeadPointerTexture, PPLLHeadPointerTexture);
    graph->RegisterResource(BuiltinResource::PPLLBuffer, PPLLBuffer);
    graph->RegisterResource(BuiltinResource::PPLLCounter, PPLLCounter);

    auto PPLLFillBuilder = graph->BuildRenderPass("PPFillPass")
        .WithUnorderedAccess(BuiltinResource::PPLLHeadPointerTexture, BuiltinResource::PPLLBuffer, BuiltinResource::PPLLCounter)
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
        gtao ? graph->RequestResource<PixelBuffer>(BuiltinResource::GTAO_OutputAOTerm)->GetSRVInfo(0).index : 0,
        graph->RequestResource<PixelBuffer>(BuiltinResource::GBuf_Normals)->GetSRVInfo(0).index);

    graph->BuildRenderPass("PPLLResolvePass")
        .WithShaderResource(BuiltinResource::PPLLHeadPointerTexture, BuiltinResource::PPLLBuffer)
        .WithRenderTarget(BuiltinResource::HDRColorTarget)
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
            .WithShaderResource(Subresources(BuiltinResource::HDRColorTarget, Mip{ i, 1 }))
            .WithRenderTarget(Subresources(BuiltinResource::HDRColorTarget, Mip{ i + 1, 1 }))
			.Build<BloomSamplePass>(i, false);
    }

	// Upsample numBloomMips - 1 mips of the HDR color target, starting from the last mip
    for (unsigned int i = numBloomMips-1; i > 0; i--) {
        graph->BuildRenderPass("BloomUpsamplePass" + std::to_string(i))
            .WithShaderResource(Subresources(BuiltinResource::HDRColorTarget, Mip{ i + 1, 1 }))
            .WithRenderTarget(Subresources(BuiltinResource::HDRColorTarget, Mip{ i, 1 }))
            .Build<BloomSamplePass>(i, true);
    }
    
    // Upsample and blend the first mip with the HDR color target
	graph->BuildRenderPass("BloomUpsampleAndBlendPass")
		.WithShaderResource(Subresources(BuiltinResource::HDRColorTarget, Mip{ 1, 1 }))
		.WithUnorderedAccess(Subresources(BuiltinResource::HDRColorTarget, Mip{ 0, 1 }))
		.Build<BloomBlendPass>();
}