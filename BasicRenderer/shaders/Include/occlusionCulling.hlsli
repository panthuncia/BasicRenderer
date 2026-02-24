#ifndef OCCLUSION_CULLING_HLSLI
#define OCCLUSION_CULLING_HLSLI

#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/misc/sphereScreenExtents.hlsli"

void OcclusionCulling(out bool fullyCulled, in const Camera camera, float3 viewSpaceCenter, float boundingSphereDepth, float scaledBoundingRadius, matrix viewProjection, uint depthMapDescriptorIndex)
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
        Texture2D<float> depthBuffer = ResourceDescriptorHeap[depthMapDescriptorIndex];
        occlusionDepth = float4(
            depthBuffer.SampleLevel(g_pointClamp, vUV.xy, fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, vUV.zy, fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, vUV.zw, fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, vUV.xw, fMipLevel));
    }
    else
    {
        Texture2DArray<float> depthBuffer = ResourceDescriptorHeap[depthMapDescriptorIndex];
        occlusionDepth = float4(
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.xy, camera.depthBufferArrayIndex), fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.zy, camera.depthBufferArrayIndex), fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.zw, camera.depthBufferArrayIndex), fMipLevel),
            depthBuffer.SampleLevel(g_pointClamp, float3(vUV.xw, camera.depthBufferArrayIndex), fMipLevel));
    }
    
    float fMaxOcclusionDepth = max(max(occlusionDepth.x, occlusionDepth.y), max(occlusionDepth.z, occlusionDepth.w));
    fullyCulled = fMaxOcclusionDepth < boundingSphereDepth - scaledBoundingRadius;
}

void OcclusionCullingPerspectiveTexture2D(
    out bool fullyCulled,
    in const Camera camera,
    float3 viewSpaceCenter,
    float boundingSphereDepth,
    float scaledBoundingRadius,
    uint depthMapDescriptorIndex)
{
    const float2 viewRes = float2(camera.depthResX, camera.depthResY);
    const float3 vHZB = float3(viewRes.x, viewRes.y, camera.numDepthMips);

    viewSpaceCenter.y = -viewSpaceCenter.y;
    float4 vLBRT = sphere_screen_extents(viewSpaceCenter.xyz, scaledBoundingRadius, camera.projection);
    vLBRT.x = -vLBRT.x;
    vLBRT.z = -vLBRT.z;

    const float4 vToUV = float4(0.5f, -0.5f, 0.5f, -0.5f);
    float4 vUV = saturate(vLBRT.xwzy * vToUV + 0.5f);

    float4 vAABB = vUV * vHZB.xyxy;
    float2 vExtents = vAABB.zw - vAABB.xy;

    float fMipLevel = ceil(log2(max(vExtents.x, vExtents.y)));
    fMipLevel = clamp(fMipLevel, 0.0f, vHZB.z - 1.0f);

    Texture2D<float> depthBuffer = ResourceDescriptorHeap[depthMapDescriptorIndex];

    // Convert viewport UVs to padded HZB UVs
    float4 vUVPadded = vUV * camera.UVScaleToNextPowerOf2.xyxy;

    // Reconstruct padded base resolution from view resolution and UV scale.
    const float2 safeScale = max(camera.UVScaleToNextPowerOf2, float2(1e-6f, 1e-6f));
    const uint mipLevel = (uint)fMipLevel;
    const uint2 hzbRes = max(uint2(1, 1), (uint2)round(viewRes / safeScale));
    const uint2 mipRes = max(uint2(1, 1), hzbRes >> mipLevel);
    const uint4 maxCoord = uint4(mipRes.xy - 1, mipRes.xy - 1);
    const uint4 pixelCoords = min((uint4)floor(vUVPadded * float4(mipRes.xy, mipRes.xy)), maxCoord);

    // xy = left bottom
    // zy = right bottom
    // zw = right top
    // xw = left top
    float4 occlusionDepth = float4(
        depthBuffer.Load(int3(pixelCoords.xy, (int)mipLevel)),
        depthBuffer.Load(int3(pixelCoords.zy, (int)mipLevel)),
        depthBuffer.Load(int3(pixelCoords.zw, (int)mipLevel)),
        depthBuffer.Load(int3(pixelCoords.xw, (int)mipLevel)));

    const float fMaxOcclusionDepth = max(max(occlusionDepth.x, occlusionDepth.y), max(occlusionDepth.z, occlusionDepth.w));
    //float epsilon = 0.001f;
    fullyCulled = fMaxOcclusionDepth < boundingSphereDepth - scaledBoundingRadius;
}

#endif // OCCLUSION_CULLING_HLSLI