#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "fullscreenVS.hlsli"
#include "include/gammaCorrection.hlsli"
#include "include/outputTypes.hlsli"
#include "include/debugPayload.hlsli"

float4 PSMain(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    // The fullscreen triangle covers the output resolution.
    // Map the UV back to render-resolution pixel coordinates.
    float2 uv = input.uv;
    uv.y = 1.0f - uv.y;

    uint renderW = perFrameBuffer.screenResX;
    uint renderH = perFrameBuffer.screenResY;

    uint2 pixel = uint2(uv * float2(renderW, renderH));
    pixel = min(pixel, uint2(renderW - 1, renderH - 1));

    Texture2D<uint2> debugTex = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::DebugVisualization)];
    uint2 payload = debugTex[pixel];

    // Sentinel means no debug data was written, show the tonemapped scene.
    if (payload.x == DEBUG_SENTINEL && payload.y == DEBUG_SENTINEL) {
        discard;
    }

    float3 color = float3(1, 0, 1); // magenta fallback
    uint outputType = perFrameBuffer.outputType;

    switch (outputType) {
        case OUTPUT_NORMAL:
        case OUTPUT_ALBEDO:
        case OUTPUT_METALLIC:
        case OUTPUT_ROUGHNESS:
        case OUTPUT_EMISSIVE:
        case OUTPUT_AO:
        case OUTPUT_DEPTH:
        case OUTPUT_DIFFUSE_IBL:
        case OUTPUT_SPECULAR_IBL:
        case OUTPUT_MODEL_NORMALS:
        case OUTPUT_MOTION_VECTORS:
        case OUTPUT_REYES_GEOMETRY_PATH:
        case OUTPUT_VSM_PREFERRED_CLIPMAP:
        case OUTPUT_VSM_SAMPLED_CLIPMAP:
        case OUTPUT_VSM_PAGE_STATE:
        case OUTPUT_TRANSPARENT_DEPTH_COMPLEXITY:
            color = UnpackDebugFloat3(payload);
            break;
        case OUTPUT_SW_RASTER:
            color = float3(1, 0, 0);
            break;
        case OUTPUT_TRANSPARENT_NODE_COUNT:
        case OUTPUT_TRANSPARENT_RESOLVED_SAMPLE_COUNT:
            color = saturate(float(UnpackDebugUint(payload)) / 16.0f).xxx;
            break;
        case OUTPUT_MESHLETS:
        case OUTPUT_LIGHT_CLUSTER_ID:
        case OUTPUT_VSM_PHYSICAL_PAGE:
            color = HashToColor(UnpackDebugUint(payload));
            break;
        case OUTPUT_LIGHT_CLUSTER_LIGHT_COUNT:
            color = HashToColor(UnpackDebugUint(payload));
            break;
    }

    color = LinearToSRGB(color);
    return float4(color, 1.0);
}
