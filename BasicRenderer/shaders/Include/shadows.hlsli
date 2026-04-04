#ifndef __SHADOWS_HLSLI__
#define __SHADOWS_HLSLI__

#include "include/cbuffers.hlsli"
#include "include/clodVirtualShadowClipmap.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"

static const uint kCLodVirtualShadowDebugFlagPreferredAllocated = 0x1u;
static const uint kCLodVirtualShadowDebugFlagPreferredDirty = 0x2u;
static const uint kCLodVirtualShadowDebugFlagSampledDepthMissing = 0x4u;
static const uint kCLodVirtualShadowDebugFlagSampledPageUnwritten = 0x8u;

struct CLodVirtualShadowDebugInfo
{
    uint preferredClipmapIndex;
    uint sampledClipmapIndex;
    uint preferredPageEntry;
    uint sampledPageEntry;
    uint sampledPhysicalPageIndex;
    uint flags;
};

CLodVirtualShadowDebugInfo CLodVirtualShadowInitDebugInfo(uint preferredClipmapIndex)
{
    CLodVirtualShadowDebugInfo debugInfo;
    debugInfo.preferredClipmapIndex = preferredClipmapIndex;
    debugInfo.sampledClipmapIndex = 0xFFFFFFFFu;
    debugInfo.preferredPageEntry = 0u;
    debugInfo.sampledPageEntry = 0u;
    debugInfo.sampledPhysicalPageIndex = 0xFFFFFFFFu;
    debugInfo.flags = 0u;
    return debugInfo;
}

float3 CLodVirtualShadowDebugClipmapColor(uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: return float3(0.95f, 0.20f, 0.15f);
    case 1u: return float3(0.95f, 0.55f, 0.12f);
    case 2u: return float3(0.90f, 0.85f, 0.20f);
    case 3u: return float3(0.20f, 0.80f, 0.25f);
    case 4u: return float3(0.20f, 0.65f, 0.95f);
    case 5u: return float3(0.55f, 0.30f, 0.90f);
    default: return float3(1.0f, 0.0f, 1.0f);
    }
}

float3 CLodVirtualShadowDebugPageStateColor(CLodVirtualShadowDebugInfo debugInfo)
{
    if ((debugInfo.flags & kCLodVirtualShadowDebugFlagSampledDepthMissing) != 0u)
    {
        return float3(1.0f, 0.0f, 1.0f);
    }

    if ((debugInfo.flags & kCLodVirtualShadowDebugFlagSampledPageUnwritten) != 0u)
    {
        return float3(0.0f, 0.85f, 0.95f);
    }

    if ((debugInfo.flags & kCLodVirtualShadowDebugFlagPreferredAllocated) == 0u)
    {
        return float3(0.05f, 0.10f, 0.55f);
    }

    if ((debugInfo.flags & kCLodVirtualShadowDebugFlagPreferredDirty) != 0u)
    {
        return float3(1.0f, 0.95f, 0.10f);
    }

    if (debugInfo.sampledClipmapIndex == 0xFFFFFFFFu)
    {
        return float3(0.65f, 0.0f, 0.0f);
    }

    return float3(0.10f, 0.85f, 0.20f);
}

float calculatePointShadow(float3 fragPosWorldSpace, float3 normal, LightInfo light, StructuredBuffer<unsigned int> pointShadowCameraIndexBuffer, StructuredBuffer<Camera> cameraBuffer) {
    float3 lightToFrag = fragPosWorldSpace.xyz - light.posWorldSpace.xyz;
    lightToFrag.z = -lightToFrag.z;
    float3 worldDir = normalize(lightToFrag);

    TextureCube<float> shadowMap = ResourceDescriptorHeap[light.shadowMapIndex];
    SamplerState shadowSampler = SamplerDescriptorHeap[light.shadowSamplerIndex];
    float depthSample = shadowMap.SampleLevel(shadowSampler, worldDir, 0);
    //depthSample = unprojectDepth(depthSample, light.nearPlane, light.farPlane);
    if (depthSample == 1.0) {
        return 0.0;
    }
    
    int faceIndex = 0;
    float maxDir = max(max(abs(worldDir.x), abs(worldDir.y)), abs(worldDir.z));

    if (worldDir.x == maxDir) {
        faceIndex = 0; // +X
    }
    else if (worldDir.x == -maxDir) {
        faceIndex = 1; // -X
    }
    else if (worldDir.y == maxDir) {
        faceIndex = 2; // +Y
    }
    else if (worldDir.y == -maxDir) {
        faceIndex = 3; // -Y
    }
    else if (worldDir.z == maxDir) {
        faceIndex = 4; // +Z
    }
    else if (worldDir.z == -maxDir) {
        faceIndex = 5; // -Z
    }
    
    uint cameraIndex = pointShadowCameraIndexBuffer[light.shadowViewInfoIndex * 6 + faceIndex];
    Camera lightCamera = cameraBuffer[cameraIndex];
    float closestDepth = unprojectDepth(depthSample, light.nearPlane, light.farPlane);

    
    float4 fragPosLightProjection = mul(float4(fragPosWorldSpace.xyz, 1.0), lightCamera.viewProjection);
    //float dist = length(lightToFrag);
    float lightSpaceDepth = fragPosLightProjection.z;
    
    float shadow = 0.0;
    float bias = max(0.0005, 0.02 * (1.0 - dot(normal, worldDir.xyz)));
    shadow = lightSpaceDepth - bias > closestDepth ? 1.0 : 0.0;
    return shadow;
}

int calculateShadowCascadeIndex(float depth, uint numCascadeSplits, float4 cascadeSplits) {
    for (int i = 0; i < numCascadeSplits; i++) {
        if (depth < cascadeSplits[i]) {
            return i;
        }
    }
    return numCascadeSplits - 1;
}

float calculateDirectionalVSMShadowDetailed(float3 fragPosWorldSpace, float3 fragPosViewSpace, float3 normal, LightInfo light, uint numCascades, float4 cascadeSplits, StructuredBuffer<unsigned int> cascadeCameraIndexBuffer, StructuredBuffer<Camera> cameraBuffer, out CLodVirtualShadowDebugInfo debugInfo) {
    (void)normal;
    (void)light;
    (void)cascadeCameraIndexBuffer;

    StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodClipmapInfo)];
    StructuredBuffer<float4> directionalPageViewInfo = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodDirectionalPageViewInfo)];
    Texture2DArray<uint> pageTable = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodPageTable)];
    Texture2D<uint> physicalPages = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodPhysicalPages)];

    const uint activeClipmapCount = min(numCascades, kCLodVirtualShadowClipmapCount);
    if (activeClipmapCount == 0u)
    {
        debugInfo = CLodVirtualShadowInitDebugInfo(0u);
        return 0.0f;
    }

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    const Camera mainCamera = cameraBuffer[perFrameBuffer.mainCameraIndex];
    const uint preferredClipmapIndex = CLodVirtualShadowSelectClipmapIndex(
        fragPosWorldSpace,
        mainCamera.positionWorldSpace.xyz,
        clipmapInfos[0].texelWorldSize,
        activeClipmapCount);
    debugInfo = CLodVirtualShadowInitDebugInfo(preferredClipmapIndex);

    const CLodVirtualShadowClipmapInfo clipmapInfo = clipmapInfos[preferredClipmapIndex];
    if (!CLodVirtualShadowClipmapIsValid(clipmapInfo))
    {
        return 0.0f;
    }

    const Camera lightCamera = cameraBuffer[clipmapInfo.shadowCameraBufferIndex];
    const float4 fragPosLightSpace = mul(float4(fragPosWorldSpace, 1.0f), lightCamera.viewProjection);
    const float safeW = max(abs(fragPosLightSpace.w), 1.0e-6f);
    float3 uv = fragPosLightSpace.xyz / safeW;
    uv.xy = uv.xy * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;

    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || uv.z < 0.0f || uv.z > 1.0f)
    {
        return 0.0f;
    }

    const uint2 virtualPageCoords = CLodVirtualShadowVirtualPageCoordsFromUv(uv.xy);
    const uint2 wrappedPageCoords = CLodVirtualShadowWrappedPageCoords(virtualPageCoords, clipmapInfo);
    const uint pageEntry = pageTable.Load(int4(wrappedPageCoords, clipmapInfo.pageTableLayer, 0));
    debugInfo.preferredPageEntry = pageEntry;
    if ((pageEntry & kCLodVirtualShadowAllocatedMask) != 0u)
    {
        debugInfo.flags |= kCLodVirtualShadowDebugFlagPreferredAllocated;
    }
    if ((pageEntry & kCLodVirtualShadowDirtyMask) != 0u)
    {
        debugInfo.flags |= kCLodVirtualShadowDebugFlagPreferredDirty;
    }
    if ((pageEntry & kCLodVirtualShadowAllocatedMask) == 0u)
    {
        return 0.0f;
    }

    const uint physicalPageIndex = pageEntry & kCLodVirtualShadowPhysicalPageIndexMask;
    debugInfo.sampledClipmapIndex = preferredClipmapIndex;
    debugInfo.sampledPageEntry = pageEntry;
    debugInfo.sampledPhysicalPageIndex = physicalPageIndex;
    if ((pageEntry & kCLodVirtualShadowContentValidMask) == 0u)
    {
        debugInfo.flags |= kCLodVirtualShadowDebugFlagSampledPageUnwritten;
        return 0.0f;
    }

    row_major matrix cachedPageView = lightCamera.view;
    const uint pageViewInfoIndex =
        clipmapInfo.pageTableLayer * (kCLodVirtualShadowPageTableResolution * kCLodVirtualShadowPageTableResolution) +
        wrappedPageCoords.y * kCLodVirtualShadowPageTableResolution +
        wrappedPageCoords.x;
    const float4 cachedPageViewRow = directionalPageViewInfo[pageViewInfoIndex];
    cachedPageView[3][0] = cachedPageViewRow.x;
    cachedPageView[3][1] = cachedPageViewRow.y;
    cachedPageView[3][2] = cachedPageViewRow.z;
    cachedPageView[3][3] = cachedPageViewRow.w;
    const float4 fragPosCachedPageLightView = mul(float4(fragPosWorldSpace, 1.0f), cachedPageView);
    const float linearLightDepth = -fragPosCachedPageLightView.z;
    if (linearLightDepth <= 0.0f)
    {
        return 0.0f;
    }

    const uint2 virtualTexelCoords = CLodVirtualShadowVirtualTexelCoordsFromUv(uv.xy);
    const uint2 atlasPixel = CLodVirtualShadowPhysicalAtlasPixel(physicalPageIndex, virtualTexelCoords);

    const uint storedDepthBits = physicalPages.Load(int3(atlasPixel, 0));
    if (storedDepthBits == 0xFFFFFFFFu)
    {
        debugInfo.flags |= kCLodVirtualShadowDebugFlagSampledDepthMissing;
        return 0.0f;
    }

    const float closestDepth = asfloat(storedDepthBits);
    const float bias = 0.0008f;
    return linearLightDepth - bias > closestDepth ? 1.0f : 0.0f;

    return 0.0f;
}

float calculateDirectionalVSMShadow(float3 fragPosWorldSpace, float3 fragPosViewSpace, float3 normal, LightInfo light, uint numCascades, float4 cascadeSplits, StructuredBuffer<unsigned int> cascadeCameraIndexBuffer, StructuredBuffer<Camera> cameraBuffer) {
    CLodVirtualShadowDebugInfo debugInfo;
    return calculateDirectionalVSMShadowDetailed(fragPosWorldSpace, fragPosViewSpace, normal, light, numCascades, cascadeSplits, cascadeCameraIndexBuffer, cameraBuffer, debugInfo);
}


float calculateCascadedShadow(float3 fragPosWorldSpace, float3 fragPosViewSpace, float3 normal, LightInfo light, uint numCascades, float4 cascadeSplits, StructuredBuffer<unsigned int> cascadeCameraIndexBuffer, StructuredBuffer<Camera> cameraBuffer) {
    return calculateDirectionalVSMShadow(fragPosWorldSpace, fragPosViewSpace, normal, light, numCascades, cascadeSplits, cascadeCameraIndexBuffer, cameraBuffer);
}

float calculateSpotShadow(float3 fragPosWorldSpace, float3 normal, LightInfo light, matrix lightMatrix, float near, float far) {
    float4 fragPosLightProjection = mul(float4(fragPosWorldSpace, 1.0), lightMatrix);
    float3 uv = fragPosLightProjection.xyz / fragPosLightProjection.w;
    uv.xy = uv.xy * 0.5 + 0.5; // Map to [0, 1] // In OpenGL this would include z, DirectX doesn't need it
    uv.y = 1.0 - uv.y;
        
    Texture2D<float> shadowMap = ResourceDescriptorHeap[light.shadowMapIndex];
    SamplerState shadowSampler = SamplerDescriptorHeap[light.shadowSamplerIndex];
    float closestDepth = unprojectDepth(shadowMap.SampleLevel(shadowSampler, uv.xy, 0).r, near, far);
    float currentDepth = fragPosLightProjection.z;
    
    // Scale bias with difference between light direction and normal
    float bias = max(0.0005, 0.01 * (1.0 - dot(normal, light.dirWorldSpace.xyz)));
    
    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
    return shadow;
}

#endif //__SHADOWS_HLSLI__