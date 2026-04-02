#ifndef __SHADOWS_HLSLI__
#define __SHADOWS_HLSLI__

#include "include/structs.hlsli"
#include "include/utilities.hlsli"

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

struct CLodVirtualShadowClipmapInfo
{
    float worldOriginX;
    float worldOriginY;
    float worldOriginZ;
    float texelWorldSize;
    uint pageOffsetX;
    uint pageOffsetY;
    uint pageTableLayer;
    uint shadowCameraBufferIndex;
    uint flags;
    uint pad0;
    uint pad1;
    uint pad2;
};

float calculateDirectionalVSMShadow(float3 fragPosWorldSpace, float3 fragPosViewSpace, float3 normal, LightInfo light, uint numCascades, float4 cascadeSplits, StructuredBuffer<unsigned int> cascadeCameraIndexBuffer, StructuredBuffer<Camera> cameraBuffer) {
    (void)fragPosViewSpace;
    (void)normal;
    (void)light;
    (void)numCascades;
    (void)cascadeSplits;
    (void)cascadeCameraIndexBuffer;

    static const uint kCLodVirtualShadowClipmapValidFlag = 0x1u;
    static const uint kCLodVirtualShadowAllocatedMask = 0x80000000u;
    static const uint kCLodVirtualShadowPhysicalPageIndexMask = 0x3FFFFFFFu;
    static const uint kCLodVirtualShadowClipmapCount = 6u;
    static const uint kCLodVirtualShadowVirtualResolution = 4096u;
    static const uint kCLodVirtualShadowPhysicalPageSize = 128u;
    static const uint kCLodVirtualShadowPhysicalPagesPerAxis = 64u;
    static const uint kCLodVirtualShadowPageTableResolution = kCLodVirtualShadowVirtualResolution / kCLodVirtualShadowPhysicalPageSize;
    static const uint kInvalidShadowCameraIndex = 0xFFFFFFFFu;

    StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodClipmapInfo)];
    Texture2DArray<uint> pageTable = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodPageTable)];
    Texture2D<uint> physicalPages = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodPhysicalPages)];

    [loop]
    for (uint clipmapIndex = 0u; clipmapIndex < kCLodVirtualShadowClipmapCount; ++clipmapIndex)
    {
        const CLodVirtualShadowClipmapInfo clipmapInfo = clipmapInfos[clipmapIndex];
        if ((clipmapInfo.flags & kCLodVirtualShadowClipmapValidFlag) == 0u ||
            clipmapInfo.shadowCameraBufferIndex == kInvalidShadowCameraIndex)
        {
            continue;
        }

        const Camera lightCamera = cameraBuffer[clipmapInfo.shadowCameraBufferIndex];
        const float4 fragPosLightSpace = mul(float4(fragPosWorldSpace, 1.0f), lightCamera.viewProjection);
        const float safeW = max(abs(fragPosLightSpace.w), 1.0e-6f);
        float3 uv = fragPosLightSpace.xyz / safeW;
        uv.xy = uv.xy * 0.5f + 0.5f;
        uv.y = 1.0f - uv.y;

        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || uv.z < 0.0f || uv.z > 1.0f) {
            continue;
        }

        const float4 fragPosLightView = mul(float4(fragPosWorldSpace, 1.0f), lightCamera.view);
        const float linearLightDepth = -fragPosLightView.z;
        if (linearLightDepth <= 0.0f)
        {
            continue;
        }

        const uint pageX = min((uint)(uv.x * kCLodVirtualShadowPageTableResolution), kCLodVirtualShadowPageTableResolution - 1u);
        const uint pageY = min((uint)(uv.y * kCLodVirtualShadowPageTableResolution), kCLodVirtualShadowPageTableResolution - 1u);
        const uint pageEntry = pageTable.Load(int4(pageX, pageY, clipmapInfo.pageTableLayer, 0));
        if ((pageEntry & kCLodVirtualShadowAllocatedMask) == 0u) {
            continue;
        }

        const uint physicalPageIndex = pageEntry & kCLodVirtualShadowPhysicalPageIndexMask;
        const uint atlasPageX = physicalPageIndex % kCLodVirtualShadowPhysicalPagesPerAxis;
        const uint atlasPageY = physicalPageIndex / kCLodVirtualShadowPhysicalPagesPerAxis;
        const uint virtualTexelX = min((uint)(uv.x * kCLodVirtualShadowVirtualResolution), kCLodVirtualShadowVirtualResolution - 1u);
        const uint virtualTexelY = min((uint)(uv.y * kCLodVirtualShadowVirtualResolution), kCLodVirtualShadowVirtualResolution - 1u);
        const uint2 atlasPixel = uint2(
            atlasPageX * kCLodVirtualShadowPhysicalPageSize + (virtualTexelX % kCLodVirtualShadowPhysicalPageSize),
            atlasPageY * kCLodVirtualShadowPhysicalPageSize + (virtualTexelY % kCLodVirtualShadowPhysicalPageSize));

        const uint storedDepthBits = physicalPages.Load(int3(atlasPixel, 0));
        if (storedDepthBits == 0xFFFFFFFFu) {
            continue;
        }

        const float closestDepth = asfloat(storedDepthBits);
        const float bias = 0.0008f;
        return linearLightDepth - bias > closestDepth ? 1.0f : 0.0f;
    }

    return 0.0f;
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