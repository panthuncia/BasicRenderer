#pragma once

enum TonemapType {
	REINHARD_JODIE = 0,
	KRONOS_PBR_NEUTRAL = 1,
	ACES_HILL = 2
};

inline std::vector<std::string> TonemapTypeNames = {
	"Reinhard-Jodie",
	"Kronos PBR Neutral",
	"ACES-Hill"
};