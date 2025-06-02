#pragma once

#include <unordered_map>
#include <string_view>
#include <optional>

enum class BuiltinResource {
	PrimaryCameraLinearDepthMap, // the linear depth map used for occlusion culling
	PrimaryCameraDepthTexture, // the actual DX12 texture used for depth
	PerObjectBuffer,
	PerMeshBuffer,
	PreSkinningVertices,
	PostSkinningVertices,
	NormalMatrixBuffer,
	MeshResourceGroup, // (all mesh data in one group)
	PrimaryCameraMeshletBitfield, // the buffer for primary camera's meshlet culling
	MeshletCullingBitfieldGroup,
	MeshInstanceMeshletCullingBitfieldGroup,
	MeshInstanceOcclusionCullingBitfieldGroup,
    //EnvironmentInfoBuffer,
    //EnvironmentPrefilteredCubemap,
    CameraBuffer,
	LightViewResourceGroup,
    LightBufferGroup,
    LightClusterBuffer,
    LightPagesBuffer,
	LightPagesCounter,
	// Environments
	EnvironmentsInfoBuffer,
	EnvironmentPrefilteredCubemapsGroup,
	WorkingEnvironmentHDRIGroup,
	WorkingEnvironmentCubemapGroup,
	CurrentEnvironmentCubemap,
	// Indirect commands
	PrimaryIndirectCommandBuffers,
	MeshletCullingCommandBuffers,
    // Gbuffer
    GBuf_Normals,
    GBuf_Albedo,
    GBuf_MetallicRoughness,
    GBuf_Emissive,
    // GTAO scratch textures
    GTAO_WorkingDepths,
    GTAO_WorkingEdges,
    GTAO_WorkingAOTerm1,
    GTAO_WorkingAOTerm2,
    GTAO_OutputAOTerm,
	// Shadows
	ShadowMaps,
	LinearShadowMaps,
	DebugTexture,
	//PPLL
	PPLLHeadPointerTexture,
	PPLLBuffer,
	PPLLCounter,
	// Color
	HDRColorTarget,
	SRGBColorTarget,
};

inline static std::unordered_map<BuiltinResource, std::string_view> builtinResourceNames = {
    {BuiltinResource::PrimaryCameraDepthTexture, "PrimaryCameraDepthTexture"},
	{BuiltinResource::PrimaryCameraLinearDepthMap, "PrimaryCameraLinearDepthMap"},
	{BuiltinResource::PerObjectBuffer, "PerObjectBuffer"},
	{BuiltinResource::PerMeshBuffer, "PerMeshBuffer"},
	{BuiltinResource::PreSkinningVertices, "PreSkinningVertices"},
	{BuiltinResource::PostSkinningVertices, "PostSkinningVertices"},
	{BuiltinResource::NormalMatrixBuffer, "NormalMatrixBuffer"},
	{BuiltinResource::MeshResourceGroup, "MeshResourceGroup"},
	{BuiltinResource::PrimaryCameraMeshletBitfield, "PrimaryCameraMeshletBitfield"},
	{BuiltinResource::MeshletCullingBitfieldGroup, "MeshletCullingBitfieldGroup"},
	{BuiltinResource::MeshInstanceMeshletCullingBitfieldGroup, "MeshInstanceMeshletCullingBitfieldGroup"},
	{BuiltinResource::MeshInstanceOcclusionCullingBitfieldGroup, "MeshInstanceOcclusionCullingBitfieldGroup"},
	//{BuiltinResource::EnvironmentInfoBuffer, "EnvironmentInfoBuffer"},
	//{BuiltinResource::EnvironmentPrefilteredCubemap, "EnvironmentPrefilteredCubemap"},
	{BuiltinResource::CameraBuffer, "CameraBuffer"},
	{BuiltinResource::LightViewResourceGroup, "LightViewResourceGroup"},
	{BuiltinResource::LightBufferGroup, "LightBufferGroup"},
	{BuiltinResource::LightClusterBuffer, "LightClusterBuffer"},
	{BuiltinResource::LightPagesBuffer, "LightPagesBuffer"},
	{BuiltinResource::LightPagesCounter, "LightPagesCounter"},
	{BuiltinResource::PrimaryIndirectCommandBuffers, "PrimaryIndirectCommandBuffers"},
	{BuiltinResource::MeshletCullingCommandBuffers, "MeshletCullingCommandBuffers"},
	{BuiltinResource::EnvironmentsInfoBuffer, "EnvironmentInfoBuffer"},
	{BuiltinResource::EnvironmentPrefilteredCubemapsGroup, "EnvironmentPrefilteredCubemapsGroup"},
	{BuiltinResource::WorkingEnvironmentHDRIGroup, "WorkingEnvironmentHDRIGroup"},
	{BuiltinResource::WorkingEnvironmentCubemapGroup, "WorkingEnvironmentCubemapGroup"},
	{BuiltinResource::CurrentEnvironmentCubemap, "CurrentEnvironmentCubemap"},
	{BuiltinResource::GBuf_Normals, "GBuf_Normals"},
	{BuiltinResource::GBuf_Albedo, "GBuf_Albedo"},
	{BuiltinResource::GBuf_MetallicRoughness, "GBuf_MetallicRoughness"},
	{BuiltinResource::GBuf_Emissive, "GBuf_Emissive"},
	{BuiltinResource::GTAO_WorkingDepths, "GTAO_WorkingDepths"},
	{BuiltinResource::GTAO_WorkingEdges, "GTAO_WorkingEdges"},
	{BuiltinResource::GTAO_WorkingAOTerm1, "GTAO_WorkingAOTerm1"},
	{BuiltinResource::GTAO_WorkingAOTerm2, "GTAO_WorkingAOTerm2"},
	{BuiltinResource::GTAO_OutputAOTerm, "GTAO_OutputAOTerm"},
	{BuiltinResource::ShadowMaps, "ShadowMaps"},
	{BuiltinResource::LinearShadowMaps, "LinearShadowMaps"},
	{BuiltinResource::DebugTexture, "DebugTexture"},
	{BuiltinResource::PPLLHeadPointerTexture, "PPLLHeadPointerTexture"},
	{BuiltinResource::PPLLBuffer, "PPLLBuffer"},
	{BuiltinResource::PPLLCounter, "PPLLCounter"},
	{BuiltinResource::HDRColorTarget, "HDRColorTarget"},
	{BuiltinResource::SRGBColorTarget, "SRGBColorTarget"},
};

inline static std::unordered_map<std::string_view, BuiltinResource> inverseBuiltinResourceNames = {
	{"PrimaryCameraDepthTexture", BuiltinResource::PrimaryCameraDepthTexture},
	{"PrimaryCameraLinearDepthMap", BuiltinResource::PrimaryCameraLinearDepthMap},
	{"PerObjectBuffer", BuiltinResource::PerObjectBuffer},
	{"PerMeshBuffer", BuiltinResource::PerMeshBuffer},
	{"PreSkinningVertices", BuiltinResource::PreSkinningVertices},
	{"PostSkinningVertices", BuiltinResource::PostSkinningVertices},
	{"NormalMatrixBuffer", BuiltinResource::NormalMatrixBuffer},
	{"MeshResourceGroup", BuiltinResource::MeshResourceGroup},
	{"PrimaryCameraMeshletBitfield", BuiltinResource::PrimaryCameraMeshletBitfield},
	{"MeshletCullingBitfieldGroup", BuiltinResource::MeshletCullingBitfieldGroup},
	{"MeshInstanceMeshletCullingBitfieldGroup", BuiltinResource::MeshInstanceMeshletCullingBitfieldGroup},
	{"MeshInstanceOcclusionCullingBitfieldGroup", BuiltinResource::MeshInstanceOcclusionCullingBitfieldGroup},
	//{"EnvironmentInfoBuffer", BuiltinResource::EnvironmentInfoBuffer},
	//{"EnvironmentPrefilteredCubemap", BuiltinResource::EnvironmentPrefilteredCubemap},
	{"CameraBuffer", BuiltinResource::CameraBuffer},
	{"LightViewResourceGroup", BuiltinResource::LightViewResourceGroup},
	{"LightBufferGroup", BuiltinResource::LightBufferGroup},
	{"ClusterBuffer", BuiltinResource::LightClusterBuffer},
	{"LightPagesBuffer", BuiltinResource::LightPagesBuffer},
	{"LightPagesCounter", BuiltinResource::LightPagesCounter},
	{"PrimaryIndirectCommandBuffers", BuiltinResource::PrimaryIndirectCommandBuffers},
	{"MeshletCullingCommandBuffers", BuiltinResource::MeshletCullingCommandBuffers},
	{"EnvironmentInfoBuffer", BuiltinResource::EnvironmentsInfoBuffer},
	{"EnvironmentPrefilteredCubemapsGroup", BuiltinResource::EnvironmentPrefilteredCubemapsGroup},
	{"WorkingEnvironmentHDRIGroup", BuiltinResource::WorkingEnvironmentHDRIGroup},
	{"WorkingEnvironmentCubemapGroup", BuiltinResource::WorkingEnvironmentCubemapGroup},
	{"CurrentEnvironmentCubemap", BuiltinResource::CurrentEnvironmentCubemap},
	{"GBuf_Normals", BuiltinResource::GBuf_Normals},
	{"GBuf_Albedo", BuiltinResource::GBuf_Albedo},
	{"GBuf_MetallicRoughness", BuiltinResource::GBuf_MetallicRoughness},
	{"GBuf_Emissive", BuiltinResource::GBuf_Emissive},
	{"GTAO_WorkingDepths", BuiltinResource::GTAO_WorkingDepths},
	{"GTAO_WorkingEdges", BuiltinResource::GTAO_WorkingEdges},
	{"GTAO_WorkingAOTerm1", BuiltinResource::GTAO_WorkingAOTerm1},
	{"GTAO_WorkingAOTerm2", BuiltinResource::GTAO_WorkingAOTerm2},
	{"GTAO_OutputAOTerm", BuiltinResource::GTAO_OutputAOTerm},
	{"ShadowMaps", BuiltinResource::ShadowMaps},
	{"LinearShadowMaps", BuiltinResource::LinearShadowMaps},
	{"DebugTexture", BuiltinResource::DebugTexture},
	{"PPLLHeadPointerTexture", BuiltinResource::PPLLHeadPointerTexture},
	{"PPLLBuffer", BuiltinResource::PPLLBuffer},
	{"PPLLCounter", BuiltinResource::PPLLCounter},
	{"HDRColorTarget", BuiltinResource::HDRColorTarget},
	{"SRGBColorTarget", BuiltinResource::SRGBColorTarget},
};

[[nodiscard]]
inline std::string_view BuiltinResourceToString(BuiltinResource r) {
    return builtinResourceNames.at(r);
}

[[nodiscard]]
inline std::optional<BuiltinResource> BuiltinResourceFromString(std::string_view s) {
	return inverseBuiltinResourceNames.find(s) != inverseBuiltinResourceNames.end()
		? std::make_optional(inverseBuiltinResourceNames.at(s))
		: std::nullopt;
}