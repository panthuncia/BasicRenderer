#ifndef OCCLUSION_CULLING_HLSLI
#define OCCLUSION_CULLING_HLSLI

#include "cbuffers.hlsli"
#include "structs.hlsli"
#include "Misc/sphereScreenExtents.hlsli"

void OcclusionCulling(out bool fullyCulled, in const Camera camera, float3 viewSpaceCenter, float boundingSphereDepth, float scaledBoundingRadius, matrix viewProjection)
{
    // Occlusion culling
    float3 vHZB = float3(camera.depthResX, camera.depthResY, camera.numDepthMips);
    viewSpaceCenter.y = -viewSpaceCenter.y; // Invert Y for HZB sampling
    float4 vLBRT;

    if (camera.isOrtho)
    {
        viewSpaceCenter.y = -viewSpaceCenter.y;
        vLBRT = sphere_screen_extents_ortho(viewSpaceCenter.xyz, scaledBoundingRadius, camera.projection);
    }
    else
    {
        vLBRT = sphere_screen_extents(viewSpaceCenter.xyz, scaledBoundingRadius, camera.projection);
        vLBRT.x = -vLBRT.x; // TODO: Fix this in sphere_screen_extents
        vLBRT.z = -vLBRT.z;
    }

    float4 vToUV = float4(0.5f, -0.5f, 0.5f, -0.5f);
    float4 vUV = saturate(vLBRT.xwzy * vToUV + 0.5f);
    float4 vAABB = vUV * vHZB.xyxy; // vHZB = [w, h, l]
    float2 vExtents = vAABB.zw - vAABB.xy; // In pixels
    
    float fMipLevel = ceil(log2(max(vExtents.x, vExtents.y)));
    fMipLevel = clamp(fMipLevel, 0.0f, vHZB.z - 1.0f);
    
    vUV *= camera.UVScaleToNextPowerOf2.xyxy; // Scale to next power of two, because it was padded for downsampling
    
    float4 occlusionDepth;
    if (camera.depthBufferArrayIndex < 0)
    { // Not a texture array
        Texture2D<float> depthBuffer = ResourceDescriptorHeap[UintRootConstant2];
        occlusionDepth = float4(
            depthBuffer.SampleLevel(g_pointClamp, vUV.xy, fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, vUV.zy, fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, vUV.zw, fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, vUV.xw, fMipLevel));
    }
    else
    {
        Texture2DArray<float> depthBuffer = ResourceDescriptorHeap[UintRootConstant2];
        occlusionDepth = float4(
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.xy, camera.depthBufferArrayIndex), fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.zy, camera.depthBufferArrayIndex), fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.zw, camera.depthBufferArrayIndex), fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.xw, camera.depthBufferArrayIndex), fMipLevel));
    }
    
    float fMaxOcclusionDepth = max(max(occlusionDepth.x, occlusionDepth.y), max(occlusionDepth.z, occlusionDepth.w));
    fullyCulled = fMaxOcclusionDepth < boundingSphereDepth - scaledBoundingRadius;
}

#endif // OCCLUSION_CULLING_HLSLI