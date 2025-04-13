#pragma once

enum OutputType {
	COLOR = 0,
	NORMAL = 1,
	ALBEDO = 2,
	METALLIC = 3,
	ROUGHNESS = 4,
	EMISSIVE = 5,
	AO = 6,
	DEPTH = 7,
	METAL_BRDF_IBL = 8,
	DIELECTRIC_BRDF_IBL = 9,
	SPECULAR_IBL = 10,
	METAL_FRESNEL_IBL = 11,
	DIELECTRIC_FRESNEL_IBL = 12,
	MESHLETS = 13,
	MODEL_NORMAL = 14,
	LIGHT_CLUSTER_ID = 15,
};

inline std::vector<std::string> OutputTypeNames = {
	"Color",
	"Normal",
	"Albedo",
	"Metallic",
	"Roughness",
	"Emissive",
	"Ambient Occlusion",
	"Depth",
	"Metallic BRDF IBL",
	"Dielectric BRDF IBL",
	"Specular IBL",
	"Metal Fresnel IBL",
	"Dielectric Fresnel IBL",
	"Meshlets",
	"Model Normal",
	"Light Cluster ID",
};