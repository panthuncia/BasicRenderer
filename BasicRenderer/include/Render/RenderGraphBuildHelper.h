#pragma once
#include "Scene/Components.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "../../generated/BuiltinResources.h"
#include "RenderPasses/PostProcessing/BloomSamplePass.h"
#include "RenderPasses/PostProcessing/BloomBlendPass.h"
#include "RenderPasses/VisibilityBufferPass.h"
#include "RenderPasses/GBufferConstructionPass.h"
#include "RenderPasses/PrimaryDepthCopyPass.h"
#include "RenderPasses/VisUtil/BuildPixelListPass.h"
#include "RenderPasses/VisUtil/EvaluateMaterialGroupsPass.h"
#include "RenderPasses/VisUtil/MaterialHistogramPass.h"
#include "RenderPasses/VisUtil/MaterialPixelCounterResetPass.h"
#include "RenderPasses/VisUtil/MaterialBlockScanPass.h"
#include "RenderPasses/VisUtil/MaterialBlockOffsetsPass.h"
#include "RenderPasses/VisUtil/BuildMaterialIndirectCommandBufferPass.h"
#include "RenderPasses/brdfIntegrationPass.h"
#include "RenderPasses/ShadowPass.h"
#include "RenderPasses/GBuffer.h"
#include "RenderPasses/GTAO/XeGTAODenoisePass.h"
#include "RenderPasses/GTAO/XeGTAOFilterPass.h"
#include "RenderPasses/GTAO/XeGTAOMainPass.h"
#include "RenderPasses/LightCullingPass.h"
#include "RenderPasses/ClusterGenerationPass.h"
#include "RenderPasses/EnvironmentConversionPass.h"
#include "RenderPasses/EnvironmentSHPass.h"
#include "RenderPasses/DeferredShadingPass.h"
#include "RenderPasses/PPLLFillPass.h"
#include "RenderPasses/PPLLResolvePass.h"
#include "RenderPasses/PostProcessing/ScreenSpaceReflectionsPass.h"
#include "RenderPasses/PostProcessing/SpecularIBLPass.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "RenderPasses/FidelityFX/LinearDepthHistoryCopyPass.h"
#include "Resources/Buffers/Buffer.h"
#include "Render/MemoryIntrospectionAPI.h"

void CreateGBufferResources(RenderGraph* graph) {
    // GBuffer resources
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();

    TextureDescription normalsWorldSpaceDesc;
    normalsWorldSpaceDesc.channels = 3;
    normalsWorldSpaceDesc.format = rhi::Format::R32G32B32A32_Typeless;
    normalsWorldSpaceDesc.hasRTV = true;
	normalsWorldSpaceDesc.rtvFormat = rhi::Format::R32G32B32A32_Float;
    normalsWorldSpaceDesc.hasSRV = true;
    normalsWorldSpaceDesc.srvFormat = rhi::Format::R32G32B32A32_Float;
    normalsWorldSpaceDesc.hasUAV = true;
	normalsWorldSpaceDesc.hasNonShaderVisibleUAV = true;
	normalsWorldSpaceDesc.uavFormat = rhi::Format::R32G32B32A32_Float;
    normalsWorldSpaceDesc.allowAlias = true;
    ImageDimensions dims = { resolution.x, resolution.y, 0, 0 };
    normalsWorldSpaceDesc.imageDimensions.push_back(dims);
    auto normalsWorldSpace = PixelBuffer::CreateSharedUnmaterialized(normalsWorldSpaceDesc);
    normalsWorldSpace->SetName("Normals World Space");
    rg::memory::SetResourceUsageHint(*normalsWorldSpace, "Visibility Buffer Resources");

    graph->RegisterResource(Builtin::GBuffer::Normals, normalsWorldSpace);

    std::shared_ptr<PixelBuffer> albedo;
    std::shared_ptr<PixelBuffer> metallicRoughness;
    std::shared_ptr<PixelBuffer> emissive;

    TextureDescription albedoDesc;
    albedoDesc.channels = 4;
    albedoDesc.hasRTV = true;
    albedoDesc.format = rhi::Format::R8G8B8A8_UNorm;
    albedoDesc.hasSRV = true;
	albedoDesc.hasUAV = true;
    albedoDesc.hasNonShaderVisibleUAV = true;
    ImageDimensions albedoDims = { resolution.x, resolution.y, 0, 0 };
    albedoDesc.imageDimensions.push_back(albedoDims);
    albedoDesc.allowAlias = true;
    albedo = PixelBuffer::CreateSharedUnmaterialized(albedoDesc);
    albedo->SetName("Albedo");
    rg::memory::SetResourceUsageHint(*albedo, "GBuffer");
    graph->RegisterResource(Builtin::GBuffer::Albedo, albedo);

    TextureDescription metallicRoughnessDesc;
    metallicRoughnessDesc.channels = 2;
    metallicRoughnessDesc.hasRTV = true;
    metallicRoughnessDesc.format = rhi::Format::R8G8_UNorm;
    metallicRoughnessDesc.hasSRV = true;
	metallicRoughnessDesc.hasUAV = true;
	metallicRoughnessDesc.hasNonShaderVisibleUAV = true;
	metallicRoughnessDesc.allowAlias = true;
    ImageDimensions metallicRoughnessDims = { resolution.x, resolution.y, 0, 0 };
    metallicRoughnessDesc.imageDimensions.push_back(metallicRoughnessDims);
    metallicRoughness = PixelBuffer::CreateSharedUnmaterialized(metallicRoughnessDesc);
    metallicRoughness->SetName("Metallic Roughness");
    rg::memory::SetResourceUsageHint(*metallicRoughness, "GBuffer");
    graph->RegisterResource(Builtin::GBuffer::MetallicRoughness, metallicRoughness);

    TextureDescription emissiveDesc;
    emissiveDesc.channels = 4;
    emissiveDesc.hasRTV = true;
    emissiveDesc.format = rhi::Format::R16G16B16A16_Float;
    emissiveDesc.hasSRV = true;
	emissiveDesc.hasUAV = true;
	emissiveDesc.hasNonShaderVisibleUAV = true;
	emissiveDesc.allowAlias = true;
    ImageDimensions emissiveDims = { resolution.x, resolution.y, 0, 0 };
    emissiveDesc.imageDimensions.push_back(emissiveDims);
    emissive = PixelBuffer::CreateSharedUnmaterialized(emissiveDesc);
    emissive->SetName("Emissive");
    rg::memory::SetResourceUsageHint(*emissive, "GBuffer");
    graph->RegisterResource(Builtin::GBuffer::Emissive, emissive);
}

void CreateDebugVisualizationResources(RenderGraph* graph) {
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();

    TextureDescription debugVisDesc;
    debugVisDesc.channels = 2;
    debugVisDesc.format = rhi::Format::R32G32_UInt;
    debugVisDesc.hasUAV = true;
    debugVisDesc.uavFormat = rhi::Format::R32G32_UInt;
    debugVisDesc.hasSRV = true;
    debugVisDesc.srvFormat = rhi::Format::R32G32_UInt;
    debugVisDesc.hasNonShaderVisibleUAV = true;
    debugVisDesc.allowAlias = true;
    ImageDimensions debugVisDims = { resolution.x, resolution.y, 0, 0 };
    debugVisDesc.imageDimensions.push_back(debugVisDims);
    auto debugVisTex = PixelBuffer::CreateSharedUnmaterialized(debugVisDesc);
    debugVisTex->SetName("Debug Visualization");
    rg::memory::SetResourceUsageHint(*debugVisTex, "Debug");
    graph->RegisterResource(Builtin::DebugVisualization, debugVisTex);
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
    auto brdfIntegrationTexture = PixelBuffer::CreateSharedUnmaterialized(brdfDesc);
    brdfIntegrationTexture->SetName("BRDF Integration Texture");
    brdfIntegrationTexture->EnableIdleDematerialization(120);
	graph->RegisterResource(Builtin::BRDFLUT, brdfIntegrationTexture);

    graph->BuildRenderPass("BRDF Integration Pass")
		.Build<BRDFIntegrationPass>();
}

void BuildOcclusionCullingPipeline(RenderGraph* graph) {

	bool shadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows")();
	bool meshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool wireframeEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool visibilityRenderingEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableVisibilityRendering")();

}

void BuildGeneralCullingPipeline(RenderGraph* graph) {

}

inline void RegisterVisUtilResources(RenderGraph* graph)
{
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    const uint32_t maxPixels = resolution.x * resolution.y;

    auto& rm = ResourceManager::GetInstance();

    // Total pixel count buffer (uint[1])
    auto totalPixelCountBuffer = Buffer::CreateUnmaterializedStructuredBuffer(
        1,
        sizeof(uint32_t),
        true,
        false,
        false,
        rhi::HeapType::DeviceLocal);
    totalPixelCountBuffer->SetAllowAlias(true);
    totalPixelCountBuffer->SetName("VisUtil::TotalPixelCountBuffer");
    rg::memory::SetResourceUsageHint(*totalPixelCountBuffer, "Visibility Buffer Resources");
    graph->RegisterResource("Builtin::VisUtil::TotalPixelCountBuffer", totalPixelCountBuffer);

	// PixelRef: uint pixelXY; (packed)
    struct PixelRefPOD { uint32_t pixelXY; };
    auto pixelListBuffer = Buffer::CreateUnmaterializedStructuredBuffer(
        maxPixels,
        sizeof(PixelRefPOD),
        true,
        false,
        false,
        rhi::HeapType::DeviceLocal);
	pixelListBuffer->SetAllowAlias(true);
    pixelListBuffer->SetName("VisUtil::PixelListBuffer");
    rg::memory::SetResourceUsageHint(*pixelListBuffer, "Visibility Buffer Resources");
    graph->RegisterResource("Builtin::VisUtil::PixelListBuffer", pixelListBuffer);
}

void BuildGBufferPipeline(RenderGraph* graph) {
    RegisterVisUtilResources(graph);
    bool occlusionCulling = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	bool enableWireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool visibilityRendering = SettingsManager::GetInstance().getSettingGetter<bool>("enableVisibilityRendering")();

    if (!useMeshShaders) {
        indirect = false; // Mesh shader pipelines are required for indirect draws
	}

    // Z prepass goes before light clustering for when active cluster determination is implemented
    bool clearRTVs = false;
    if (!occlusionCulling || !indirect) {
        clearRTVs = true; // We will not run an earlier pass
    }

    if (!visibilityRendering) {
        graph->BuildRenderPass("newObjectsPrepass") // Do another prepass for any objects that aren't occluded
            .Build<GBufferPass>(GBufferPassInputs{
                enableWireframe,
                useMeshShaders,
                indirect,
                clearRTVs });
    }
    else {

        // Reset material counters
        graph->BuildComputePass("MaterialPixelCounterResetPass")
            .Build<MaterialUAVResetPass>();

        // Build material histogram
        graph->BuildComputePass("MaterialHistogramPass")
            .Build<MaterialHistogramPass>();

        // Prefix sum material histogram
        graph->BuildComputePass("MaterialBlockScanPass")
            .Build<MaterialBlockScanPass>();

        graph->BuildComputePass("MaterialBlockOffsetsPass")
            .Build<MaterialBlockOffsetsPass>();

        // Build pixel list
        graph->BuildComputePass("BuildPixelListPass")
            .Build<BuildPixelListPass>();

        // Build indirect command buffer for material passes
        graph->BuildComputePass("BuildMaterialIndirectCommandBufferPass")
            .Build<BuildMaterialIndirectCommandBufferPass>();

        // Evaluate material groups
        graph->BuildComputePass("EvaluateMaterialGroupsPass")
            .Build<EvaluateMaterialGroupsPass>();

        // PrimaryDepthCopyPass is disabled for CLod two-phase path.
    }
}

void RegisterGTAOResources(RenderGraph* graph) {
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    constexpr uint64_t gtaoAliasPoolID = 1;

    TextureDescription workingDepthsDesc;
    workingDepthsDesc.arraySize = 1;
    workingDepthsDesc.channels = 1;
    workingDepthsDesc.isCubemap = false;
    workingDepthsDesc.hasUAV = true;
	workingDepthsDesc.hasSRV = true;
    workingDepthsDesc.format = rhi::Format::R32_Float;
    workingDepthsDesc.generateMipMaps = true;
    workingDepthsDesc.allowAlias = true;
    ImageDimensions dims1 = { resolution.x, resolution.y, 0, 0 };
    workingDepthsDesc.imageDimensions.push_back(dims1);
    auto workingDepths = PixelBuffer::CreateSharedUnmaterialized(workingDepthsDesc);
    //workingDepths->SetAliasingPool(gtaoAliasPoolID);
    rg::memory::SetResourceUsageHint(*workingDepths, "GTAO resources");
    workingDepths->SetName("GTAO Working Depths");

    TextureDescription workingEdgesDesc;
    workingEdgesDesc.arraySize = 1;
    workingEdgesDesc.channels = 1;
    workingEdgesDesc.isCubemap = false;
    workingEdgesDesc.hasUAV = true;
	workingEdgesDesc.hasSRV = true;
    workingEdgesDesc.format = rhi::Format::R8_UNorm;
    workingEdgesDesc.generateMipMaps = false;
    workingEdgesDesc.imageDimensions.push_back(dims1);
	workingEdgesDesc.allowAlias = true;
    auto workingEdges = PixelBuffer::CreateSharedUnmaterialized(workingEdgesDesc);
    rg::memory::SetResourceUsageHint(*workingDepths, "GTAO resources");
    workingEdges->SetName("GTAO Working Edges");

    TextureDescription workingAOTermDesc;
    workingAOTermDesc.arraySize = 1;
    workingAOTermDesc.channels = 1;
    workingAOTermDesc.isCubemap = false;
    workingAOTermDesc.hasUAV = true;
	workingAOTermDesc.hasSRV = true;
    workingAOTermDesc.format = rhi::Format::R8_UInt;
    workingAOTermDesc.generateMipMaps = false;
    workingAOTermDesc.imageDimensions.push_back(dims1);
    workingAOTermDesc.allowAlias = true;
    auto workingAOTerm1 = PixelBuffer::CreateSharedUnmaterialized(workingAOTermDesc);
    workingAOTerm1->SetName("GTAO Working AO Term 1");
    rg::memory::SetResourceUsageHint(*workingAOTerm1, "GTAO resources");
    auto workingAOTerm2 = PixelBuffer::CreateSharedUnmaterialized(workingAOTermDesc);
    workingAOTerm2->SetName("GTAO Working AO Term 2");
    rg::memory::SetResourceUsageHint(*workingAOTerm2, "GTAO resources");
    std::shared_ptr<PixelBuffer> outputAO = PixelBuffer::CreateSharedUnmaterialized(workingAOTermDesc);
    //outputAO->SetAliasingPool(gtaoAliasPoolID);
    outputAO->SetName("GTAO Output AO Term");
    rg::memory::SetResourceUsageHint(*outputAO, "GTAO resources");

    graph->RegisterResource(Builtin::GTAO::WorkingAOTerm1, workingAOTerm1);
    graph->RegisterResource(Builtin::GTAO::WorkingAOTerm2, workingAOTerm2);
    graph->RegisterResource(Builtin::GTAO::OutputAOTerm, outputAO);
    graph->RegisterResource(Builtin::GTAO::WorkingDepths, workingDepths);
    graph->RegisterResource(Builtin::GTAO::WorkingEdges, workingEdges);
}

void BuildGTAOPipeline(RenderGraph* graph, const Components::Camera* currentCamera) {
    auto GTAOConstantBuffer = CreateIndexedConstantBuffer(sizeof(GTAOInfo),"GTAO constants");
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();

    GTAOInfo gtaoInfo;
    XeGTAO::GTAOSettings gtaoSettings;
    XeGTAO::GTAOConstants& gtaoConstants = gtaoInfo.g_GTAOConstants; // Intel's GTAO constants
    XeGTAO::GTAOUpdateConstants(gtaoConstants, resolution.x, resolution.y, gtaoSettings, false, 0, *currentCamera);

    BUFFER_UPLOAD(&gtaoInfo, sizeof(GTAOInfo), rg::runtime::UploadTarget::FromShared(GTAOConstantBuffer), 0);

    graph->RegisterResource("Builtin::GTAO::ConstantsBuffer", GTAOConstantBuffer);

    graph->BuildComputePass("GTAOFilterPass") // Depth filter pass
        .Build<GTAOFilterPass>();

    graph->BuildComputePass("GTAOMainPass") // Main pass
        .Build<GTAOMainPass>();

    graph->BuildComputePass("GTAODenoisePass") // Denoise pass
        .Build<GTAODenoisePass>();
}

void BuildLightClusteringPipeline(RenderGraph* graph) {
    // light pages counter
    auto lightPagesCounter = Buffer::CreateUnmaterializedStructuredBuffer(
        1,
        sizeof(unsigned int),
        true,
        false,
        false,
        rhi::HeapType::DeviceLocal);
    lightPagesCounter->SetName("Light Pages Counter");
    graph->RegisterResource(Builtin::Light::PagesCounter, lightPagesCounter);

    graph->BuildComputePass("ClusterGenerationPass")
        .Build<ClusterGenerationPass>();

    graph->BuildComputePass("LightCullingPass")
        .Build<LightCullingPass>();
}

void BuildEnvironmentPipeline(RenderGraph* graph) {
    graph->BuildComputePass("Environment Conversion Pass")
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
        .Build<ShadowPass>(ShadowPassInputs{ wireframe, useMeshShaders, indirect, true, clearRTVs });
}

void BuildLinearDepthDownsamplePass(RenderGraph* graph) {
    graph->BuildComputePass("LinearDepthDownsamplePass")
        .Build<DownsamplePass>();
}

void BuildLinearDepthHistoryCopyPass(RenderGraph* graph) {
    graph->BuildRenderPass("LinearDepthHistoryCopyPass")
        .Build<LinearDepthHistoryCopyPass>();
}

void BuildPrimaryPass(RenderGraph* graph, Environment* currentEnvironment) {

	bool gtaoEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableGTAO")();
	bool meshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();

	// Uses existing GBuffer resources
    graph->BuildComputePass("DeferredShadingPass").Build<DeferredShadingPass>();

	// Forward pass for materials incompatible with deferred rendering
    graph->BuildRenderPass("Forward render pass").Build<ForwardRenderPass>(ForwardRenderPassInputs{
        wireframe,
        meshShaders,
        indirect});
}

void BuildPPLLPipeline(RenderGraph* graph) {
	auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
	bool useMeshShaders = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader")();
	bool indirect = SettingsManager::GetInstance().getSettingGetter<bool>("enableIndirectDraws")();
	bool wireframe = SettingsManager::GetInstance().getSettingGetter<bool>("enableWireframe")();
    if (!useMeshShaders) {
        indirect = false; // Mesh shader pipelines are required for indirect draws
	}

    static const size_t aveFragsPerPixel = 5;
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
    desc.allowAlias = true;
    auto PPLLHeadPointerTexture = PixelBuffer::CreateSharedUnmaterialized(desc);
    PPLLHeadPointerTexture->SetName("PPLLHeadPointerTexture");
    rg::memory::SetResourceUsageHint(*PPLLHeadPointerTexture, "OIT resources");
    auto PPLLBuffer = Buffer::CreateUnmaterializedStructuredBuffer(
        static_cast<uint32_t>(numPPLLNodes),
        static_cast<uint32_t>(PPLLNodeSize),
        true,
        false,
        false,
        rhi::HeapType::DeviceLocal);
    PPLLBuffer->SetAllowAlias(true);
    PPLLBuffer->SetName("PPLLBuffer");
    rg::memory::SetResourceUsageHint(*PPLLBuffer, "OIT resources");
    auto PPLLCounter = Buffer::CreateSharedUnmaterialized(rhi::HeapType::DeviceLocal, sizeof(uint32_t), true);
    {
        BufferBase::DescriptorRequirements descReq{};
        descReq.createCBV = false;
        descReq.createSRV = true;
        descReq.createUAV = true;
        descReq.createNonShaderVisibleUAV = true;
        descReq.uavCounterOffset = 0;

        descReq.srvDesc = rhi::SrvDesc{
            .dimension = rhi::SrvDim::Buffer,
            .formatOverride = rhi::Format::R32_UInt,
            .buffer = {
                .kind = rhi::BufferViewKind::Typed,
                .firstElement = 0,
                .numElements = 1,
                .structureByteStride = 0,
            },
        };

        descReq.uavDesc = rhi::UavDesc{
            .dimension = rhi::UavDim::Buffer,
            .formatOverride = rhi::Format::R32_UInt,
            .buffer = {
                .kind = rhi::BufferViewKind::Typed,
                .firstElement = 0,
                .numElements = 1,
                .structureByteStride = 0,
                .counterOffsetInBytes = 0,
            },
        };

        PPLLCounter->SetDescriptorRequirements(descReq);
    }
    PPLLCounter->SetName("PPLLCounter");
    rg::memory::SetResourceUsageHint(*PPLLCounter, "OIT resources");

    graph->RegisterResource(Builtin::PPLL::HeadPointerTexture, PPLLHeadPointerTexture);
    graph->RegisterResource(Builtin::PPLL::DataBuffer, PPLLBuffer);
    graph->RegisterResource(Builtin::PPLL::Counter, PPLLCounter);

    auto& PPLLFillBuilder = graph->BuildRenderPass("PPFillPass");

    PPLLFillBuilder.Build<PPLLFillPass>(PPLLFillPassInputs{
        wireframe,
        numPPLLNodes,
        useMeshShaders,
        indirect });

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
			.Build<BloomSamplePass>(BloomSamplePassInputs{ i, false });
    }

	// Upsample numBloomMips - 1 mips of the HDR color target, starting from the last mip
    for (unsigned int i = numBloomMips-1; i > 0; i--) {
        graph->BuildRenderPass("BloomUpsamplePass" + std::to_string(i))
            .Build<BloomSamplePass>(BloomSamplePassInputs{ i, true });
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
    ssrDesc.allowAlias = true;
    auto ssrTexture = PixelBuffer::CreateSharedUnmaterialized(ssrDesc);
    ssrTexture->SetName("SSR Texture");
    rg::memory::SetResourceUsageHint(*ssrTexture, "Post-Processing resources");
	graph->RegisterResource(Builtin::PostProcessing::ScreenSpaceReflections, ssrTexture);

    graph->BuildComputePass("Screen-Space Reflections Pass")
		.Build<ScreenSpaceReflectionsPass>();

    graph->BuildRenderPass("Specular IBL & SSR Composite Pass")
		.Build<SpecularIBLPass>();
}