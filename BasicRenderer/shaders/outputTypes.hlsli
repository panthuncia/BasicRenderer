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
#define OUTPUT_DIFFUSE_IBL 8
#define OUTPUT_MESHLETS 9
#define OUTPUT_MODEL_NORMALS 10
#define OUTPUT_LIGHT_CLUSTER_ID 11
#define OUTPUT_LIGHT_CLUSTER_LIGHT_COUNT 12

#endif // __OUTPUT_TYPES_HLSLI__