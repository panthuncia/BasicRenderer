#pragma once
enum MaterialFlags {
	MATERIAL_FLAGS_NONE = 0,
	MATERIAL_TEXTURED = 1 << 0,
    MATERIAL_BASE_COLOR_TEXTURE = 1 << 1,
    MATERIAL_NORMAL_MAP = 1 << 2,
    MATERIAL_AO_TEXTURE = 1 << 3,
    MATERIAL_EMISSIVE_TEXTURE = 1 << 4,
    MATERIAL_PBR = 1 << 5,
    MATERIAL_PBR_MAPS = 1 << 6,
    MATERIAL_DOUBLE_SIDED = 1 << 7,
    MATERIAL_PARALLAX = 1 << 8,
	MATERIAL_NEGATE_NORMALS = 1 << 9, // Some normal textures are inverted
	MATERIAL_INVERT_NORMAL_GREEN = 1 << 10, // Some normal textures have inverted green channel
	MATERIAL_OPACITY_TEXTURE = 1 << 11,
};