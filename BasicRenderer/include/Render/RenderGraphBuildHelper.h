#pragma once
#include "Scene/Components.h"
#include "Render/RenderGraph.h"
#include "../../generated/BuiltinResources.h"
#include "RenderPasses/PostProcessing/BloomSamplePass.h"
#include "RenderPasses/PostProcessing/BloomBlendPass.h"

void BuildVisibilityPass(RenderGraph* graph) {
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();

    TextureDescription visibilityDesc;
    visibilityDesc.channels = 2;
    visibilityDesc.format = rhi::Format::R32G32_UInt;
    visibilityDesc.hasRTV = true;
    visibilityDesc.hasUAV = true;
    visibilityDesc.imageDimensions.emplace_back(resolution.x, resolution.y);
    auto visibilityTexture = PixelBuffer::Create(visibilityDesc);
    visibilityTexture->SetName(L"Visibility buffer");
    graph->RegisterResource(Builtin::PrimaryCamera::VisibilityTexture, visibilityTexture);

    /*graph->BuildRenderPass("Visibility pass")
        .Build<VisibilityPass>();*/
}

void CreateGBufferResources(RenderGraph* graph) {
    // GBuffer resources
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();

    TextureDescription normalsWorldSpaceDesc;
    normalsWorldSpaceDesc.arraySize = 1;
    normalsWorldSpaceDesc.channels = 3;
    normalsWorldSpaceDesc.isCubemap = false;
    normalsWorldSpaceDesc.format = rhi::Format::R32G32B32A32_Typeless;
    normalsWorldSpaceDesc.hasRTV = true;
	normalsWorldSpaceDesc.rtvFormat = rhi::Format::R32G32B32A32_Float;
    normalsWorldSpaceDesc.generateMipMaps = false;
    normalsWorldSpaceDesc.hasSRV = true;
    normalsWorldSpaceDesc.srvFormat = rhi::Format::R32G32B32A32_Float;
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
        albedoDesc.format = rhi::Format::R8G8B8A8_UNorm;
        albedoDesc.generateMipMaps = false;
        albedoDesc.hasSRV = true;
        albedoDesc.srvFormat = rhi::Format::R8G8B8A8_UNorm;
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
        metallicRoughnessDesc.format = rhi::Format::R8G8_UNorm;
        metallicRoughnessDesc.generateMipMaps = false;
        metallicRoughnessDesc.hasSRV = true;
        metallicRoughnessDesc.srvFormat = rhi::Format::R8G8_UNorm;
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
        emissiveDesc.format = rhi::Format::R16G16B16A16_Float;
        emissiveDesc.generateMipMaps = false;
        emissiveDesc.hasSRV = true;
        emissiveDesc.srvFormat = rhi::Format::R16G16B16A16_Float;
        ImageDimensions emissiveDims = { resolution.x, resolution.y, 0, 0 };
        emissiveDesc.imageDimensions.push_back(emissiveDims);
        emissive = PixelBuffer::Create(emissiveDesc);
        emissive->SetName(L"Emissive");
        graph->RegisterResource(Builtin::GBuffer::Emissive, emissive);
    }
}

void BuildBRDFIntegrationPass(RenderGraph* graph) {
	TextureDescription brdfDesc;
    brdfDesc.arraySize = 1;
    brdfDesc.channels = 1;
    brdfDesc.isCubemap = false;
    brdfDesc.hasRTV = true;
    brdfDesc.format = rhi::Format::R16G16_Float;
    brdfDesc.generateMipMaps = false;
    brdfDesc.hasSRV = true;
    brdfDesc.srvFormat = rhi::Format::R16G16_Float;
	brdfDesc.hasUAV = true;
	brdfDesc.uavFormat = rhi::Format::R16G16_Float;
    ImageDimensions dims = { 512, 512, 0, 0 };
    brdfDesc.imageDimensions.push_back(dims);
    auto brdfIntegrationTexture = PixelBuffer::Create(brdfDesc);
    brdfIntegrationTexture->SetName(L"BRDF Integration Texture");
	graph->RegisterResource(Builtin::BRDFLUT, brdfIntegrationTexture);

    graph->BuildRenderPass("BRDF Integration Pass")
		.Build<BRDFIntegrationPass>();
}

void BuildOcclusionCullingPipeline(RenderGraph* graph) {

	bool shadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows")();
	bool meshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool wireframeEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();

    graph->BuildRenderPass("ClearLastFrameIndirectDrawUAVsPass") // Clears indirect draws from last frame
        .Build<ClearIndirectDrawCommandUAVsPass>(false);

    graph->BuildRenderPass("ClearMeshletCullingCommandUAVsPass0") // Clear meshlet culling reset command buffers from last frame
        .Build<ClearMeshletCullingCommandUAVsPass>();

    graph->BuildComputePass("BuildOccluderDrawCommandsPass") // Builds draw command list for last frame's occluders
        .Build<ObjectCullingPass>(true, false);

    // We need to draw occluder shadows early
    auto drawShadows = graph->RequestResource(Builtin::Shadows::ShadowMaps) != nullptr && shadowsEnabled;
    if (drawShadows) {
        graph->BuildRenderPass("OccluderShadowPrepass")
            .Build<ShadowPass>(wireframeEnabled, meshShadersEnabled, true, false, true);
    }

    graph->BuildRenderPass("OccludersPrepass") // Draws prepass for last frame's occluders
        .Build<ZPrepass>(
        wireframeEnabled, 
        meshShadersEnabled, 
        true, 
        true);

    // Single-pass downsample on all occluder-only depth maps
    // TODO: Case where HZB is not conservative when downsampling mips with non-even resolutions (bottom/side pixels get dropped), handled sub-optimally
    graph->BuildComputePass("DownsamplePass")
        .Build<DownsamplePass>();

    // After downsample, we need to render the "remainders" of the occluders (meshlets that were culled last frame, but shouldn't be this frame)
    // Using occluder meshlet culling command buffer, cull meshlets, but invert the bitfield and use occlusion culling
    graph->BuildComputePass("OcclusionMeshletRemaindersCullingPass")
        .Build<MeshletCullingPass>(false, true, true);

    // Now, render the occluder remainders (prepass & shadows)
    if (drawShadows) {

        graph->BuildRenderPass("OccluderRemaindersShadowPass")
            .Build<ShadowPass>(wireframeEnabled, meshShadersEnabled, true, false, false);
    }

    graph->BuildRenderPass("OccluderRemaindersPrepass") // Draws prepass for last frame's occluders
        .Build<ZPrepass>(
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
        .Build<ClearIndirectDrawCommandUAVsPass>(true);

    graph->BuildRenderPass("ClearMeshletCullingCommandUAVsPass1") // Clear meshlet culling reset command buffers from prepass
        .Build<ClearMeshletCullingCommandUAVsPass>();

    graph->BuildComputePass("ObjectCullingPass") // Performs frustrum and occlusion culling
        .Build<ObjectCullingPass>(false, occlusionCulling);

    if (meshletCulling || occlusionCulling) {
        graph->BuildComputePass("MeshletCullingPass") // Any meshes that are partially culled are sent to the meshlet culling pass
            .Build<MeshletCullingPass>(false, false, true);
    }
}

void BuildZPrepass(RenderGraph* graph) {
    bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	bool enableWireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
    if (!useMeshShaders) {
        indirect = false; // Mesh shader pipelines are required for indirect draws
	}

    // Z prepass goes before light clustering for when active cluster determination is implemented
    bool clearRTVs = false;
    if (!occlusionCulling || !indirect) {
        clearRTVs = true; // We will not run an earlier pass
    }
    graph->BuildRenderPass("newObjectsPrepass") // Do another prepass for any objects that aren't occluded
        .Build<ZPrepass>(
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
    workingDepthsDesc.format = rhi::Format::R32_Float;
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
    workingEdgesDesc.format = rhi::Format::R8_UNorm;
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
    workingAOTermDesc.format = rhi::Format::R8_UInt;
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
	rhi::SamplerDesc samplerDesc;
	samplerDesc.minFilter = rhi::Filter::Nearest;
	samplerDesc.magFilter = rhi::Filter::Nearest;
	samplerDesc.mipFilter = rhi::MipFilter::Nearest;
	samplerDesc.addressU = rhi::AddressMode::Clamp;
	samplerDesc.addressV = rhi::AddressMode::Clamp;
	samplerDesc.addressW = rhi::AddressMode::Clamp;
	samplerDesc.mipLodBias = 0.0f;
	samplerDesc.maxAnisotropy = 1;
	samplerDesc.compareOp = rhi::CompareOp::Never;
	samplerDesc.borderPreset = rhi::BorderPreset::OpaqueBlack;

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
    gtaoInfo.g_srcRawDepthDescriptorIndex = graph->RequestResource<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture)->GetSRVInfo(0).slot.index;
    gtaoInfo.g_outWorkingDepthMIP0DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(0).slot.index;
    gtaoInfo.g_outWorkingDepthMIP1DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(1).slot.index;
    gtaoInfo.g_outWorkingDepthMIP2DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(2).slot.index;
    gtaoInfo.g_outWorkingDepthMIP3DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(3).slot.index;
    gtaoInfo.g_outWorkingDepthMIP4DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo(4).slot.index;

    // Main pass
    gtaoInfo.g_srcWorkingDepthDescriptorIndex = workingDepths->GetSRVInfo(0).slot.index;
    gtaoInfo.g_srcNormalmapDescriptorIndex = graph->RequestResource<PixelBuffer>(Builtin::GBuffer::Normals)->GetSRVInfo(0).slot.index;
    // TODO: Hilbert lookup table
    gtaoInfo.g_outWorkingAOTermDescriptorIndex = workingAOTerm1->GetUAVShaderVisibleInfo(0).slot.index;
    gtaoInfo.g_outWorkingEdgesDescriptorIndex = workingEdges->GetUAVShaderVisibleInfo(0).slot.index;

    // Denoise pass
    gtaoInfo.g_srcWorkingEdgesDescriptorIndex = workingEdges->GetSRVInfo(0).slot.index;
    gtaoInfo.g_outFinalAOTermDescriptorIndex = outputAO->GetUAVShaderVisibleInfo(0).slot.index;

    UploadManager::GetInstance().UploadData(&gtaoInfo, sizeof(GTAOInfo), GTAOConstantBuffer.get(), 0);

    graph->BuildComputePass("GTAOFilterPass") // Depth filter pass
        .Build<GTAOFilterPass>(GTAOConstantBuffer);

    graph->BuildComputePass("GTAOMainPass") // Main pass
        .Build<GTAOMainPass>(GTAOConstantBuffer);

    graph->BuildComputePass("GTAODenoisePass") // Denoise pass
        .Build<GTAODenoisePass>(GTAOConstantBuffer, workingAOTerm1->GetSRVInfo(0).slot.index);
}

void BuildLightClusteringPipeline(RenderGraph* graph) {
    // light pages counter
    auto lightPagesCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int), true, false);
    lightPagesCounter->SetName(L"Light Pages Counter");
    graph->RegisterResource(Builtin::Light::PagesCounter, lightPagesCounter);

    graph->BuildComputePass("ClusterGenerationPass")
        .Build<ClusterGenerationPass>();

    graph->BuildComputePass("LightCullingPass")
        .Build<LightCullingPass>();
}

void BuildEnvironmentPipeline(RenderGraph* graph) {
    graph->BuildRenderPass("Environment Conversion Pass")
        .Build<EnvironmentConversionPass>();

    graph->BuildComputePass("Environment Spherical Harmonics Pass")
        .Build<EnvironmentSHPass>();

    graph->BuildRenderPass("Environment Prefilter Pass")
        .Build<EnvironmentFilterPass>();
}

void BuildMainShadowPass(RenderGraph* graph) {
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	// TODO: Make a better way of evaluating dependencies between settings. Maybe a graph?
	indirect = indirect && useMeshShaders; // Mesh shader pipelines are required for indirect draws

    bool clearRTVs = false;
    if (!occlusionCulling || !indirect) {
        clearRTVs = true; // We will not run an earlier pass
    }

    graph->BuildRenderPass("ShadowPass")
        .Build<ShadowPass>(wireframe, useMeshShaders, indirect, true, clearRTVs);
}

void BuildPrimaryPass(RenderGraph* graph, Environment* currentEnvironment) {

	bool deferredRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableDeferredRendering")();
	bool gtaoEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableGTAO")();
	bool meshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();

    std::string primaryPassName = deferredRendering ? "Deferred Pass" : "Forward Pass";
    auto primaryPassBuilder = graph->BuildRenderPass(primaryPassName);

    if (deferredRendering) {
        primaryPassBuilder.Build<DeferredRenderPass>();
    }
    else {
        primaryPassBuilder.Build<ForwardRenderPass>(
            wireframe, 
            meshShaders, 
            indirect, 
            gtaoEnabled ? graph->RequestResource<PixelBuffer>(Builtin::GTAO::OutputAOTerm)->GetSRVInfo(0).slot.index : 0);
    }
}

void BuildPPLLPipeline(RenderGraph* graph) {
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
    if (!useMeshShaders) {
        indirect = false; // Mesh shader pipelines are required for indirect draws
	}

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
    desc.format = rhi::Format::R32_UInt;
    desc.hasRTV = false;
    desc.hasUAV = true;
    desc.hasNonShaderVisibleUAV = true;
    auto PPLLHeadPointerTexture = PixelBuffer::Create(desc);
    PPLLHeadPointerTexture->SetName(L"PPLLHeadPointerTexture");
    auto PPLLBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(numPPLLNodes, PPLLNodeSize, true, false);
    PPLLBuffer->SetName(L"PPLLBuffer");
	auto PPLLCounter = ResourceManager::GetInstance().CreateIndexedTypedBuffer(1, rhi::Format::R32_UInt, true);
    PPLLCounter->SetName(L"PPLLCounter");

    graph->RegisterResource(Builtin::PPLL::HeadPointerTexture, PPLLHeadPointerTexture);
    graph->RegisterResource(Builtin::PPLL::DataBuffer, PPLLBuffer);
    graph->RegisterResource(Builtin::PPLL::Counter, PPLLCounter);

    auto PPLLFillBuilder = graph->BuildRenderPass("PPFillPass");

    PPLLFillBuilder.Build<PPLLFillPass>(wireframe,
        numPPLLNodes, 
        useMeshShaders, 
        indirect);

    graph->BuildRenderPass("PPLLResolvePass")
        .Build<PPLLResolvePass>();
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
			.Build<BloomSamplePass>(i, false);
    }

	// Upsample numBloomMips - 1 mips of the HDR color target, starting from the last mip
    for (unsigned int i = numBloomMips-1; i > 0; i--) {
        graph->BuildRenderPass("BloomUpsamplePass" + std::to_string(i))
            .Build<BloomSamplePass>(i, true);
    }
    
    // Upsample and blend the first mip with the HDR color target
	graph->BuildRenderPass("BloomUpsampleAndBlendPass")
		.Build<BloomBlendPass>();
}

void BuildSSRPasses(RenderGraph* graph) {
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();

    TextureDescription ssrDesc;
    ssrDesc.arraySize = 1;
    ssrDesc.channels = 4;
    ssrDesc.isCubemap = false;
    ssrDesc.hasRTV = true;
    ssrDesc.format = rhi::Format::R16G16B16A16_Float;
    ssrDesc.generateMipMaps = false;
    ssrDesc.hasSRV = true;
    ssrDesc.srvFormat = rhi::Format::R16G16B16A16_Float;
	ssrDesc.hasUAV = true;
	ssrDesc.uavFormat = rhi::Format::R16G16B16A16_Float;
	ssrDesc.hasNonShaderVisibleUAV = true; // For ClearUnorderedAccessView
    ImageDimensions dims = { resolution.x, resolution.y, 0, 0 };
    ssrDesc.imageDimensions.push_back(dims);
    auto ssrTexture = PixelBuffer::Create(ssrDesc);
    ssrTexture->SetName(L"SSR Texture");
	graph->RegisterResource(Builtin::PostProcessing::ScreenSpaceReflections, ssrTexture);

    graph->BuildComputePass("Screen-Space Reflections Pass")
		.Build<ScreenSpaceReflectionsPass>();

    graph->BuildRenderPass("Specular IBL & SSR Composite Pass")
		.Build<SpecularIBLPass>();
}