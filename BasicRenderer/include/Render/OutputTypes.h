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
	MESHLETS = 9,
	MODEL_NORMAL = 10,
	LIGHT_CLUSTER_ID = 11,
	CLUSTERED_LIGHT_COUNT = 12,
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
	"Meshlets",
	"Model Normal",
	"Light Cluster ID",
	"Clustered Light Count",
};