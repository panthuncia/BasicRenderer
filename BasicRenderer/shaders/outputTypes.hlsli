#ifndef __OUTPUT_TYPES_HLSLI__
#define __OUTPUT_TYPES_HLSLI__

// Do enums exist in HLSL?
// The internet disagrees.
// My compiler doesn't like them though
#define OUTPUT_COLOR 0
#define OUTPUT_NORMAL 1
#define OUTPUT_ALBEDO 2
#define OUTPUT_METALLIC 3
#define OUTPUT_ROUGHNESS 4
#define OUTPUT_EMISSIVE 5
#define OUTPUT_AO 6
#define OUTPUT_DEPTH 7
#define OUTPUT_METAL_BRDF_IBL 8
#define OUTPUT_DIELECTRIC_BRDF_IBL 9
#define OUTPUT_SPECULAR_IBL 10
#define OUTPUT_METAL_FRESNEL_IBL 11
#define OUTPUT_DIELECTRIC_FRESNEL_IBL 12
#define OUTPUT_MESHLETS 13
#define OUTPUT_MODEL_NORMALS 14

#endif // __OUTPUT_TYPES_HLSLI__