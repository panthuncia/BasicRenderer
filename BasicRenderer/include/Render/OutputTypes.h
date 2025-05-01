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
	OUTPUT_DIFFUSE_IBL = 8,
	OUTPUT_SPECULAR_IBL = 9,
	MESHLETS = 10,
	MODEL_NORMAL = 11,
	LIGHT_CLUSTER_ID = 12,
	CLUSTERED_LIGHT_COUNT = 13,
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
	"Diffuse IBL",
	"Specular IBL",
	"Meshlets",
	"Model Normal",
	"Light Cluster ID",
	"Clustered Light Count",
};