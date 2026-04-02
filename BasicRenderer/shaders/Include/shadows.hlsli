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

float calculateDirectionalVSMShadow(float3 fragPosWorldSpace, float3 fragPosViewSpace, float3 normal, LightInfo light, uint numCascades, float4 cascadeSplits, StructuredBuffer<unsigned int> cascadeCameraIndexBuffer, StructuredBuffer<Camera> cameraBuffer) {
    static const uint kCLodVirtualShadowAllocatedMask = 0x80000000u;
    static const uint kCLodVirtualShadowDirtyMask = 0x40000000u;
    static const uint kCLodVirtualShadowPhysicalPageIndexMask = 0x3FFFFFFFu;
    static const uint kCLodVirtualShadowVirtualResolution = 4096u;
    static const uint kCLodVirtualShadowPhysicalPageSize = 128u;
    static const uint kCLodVirtualShadowPhysicalPagesPerAxis = 64u;
    static const uint kCLodVirtualShadowPageTableResolution = kCLodVirtualShadowVirtualResolution / kCLodVirtualShadowPhysicalPageSize;
    static const uint kInvalidShadowCameraIndex = 0xFFFFFFFFu;

    float depth = abs(fragPosViewSpace.z);
    int cascadeIndex = calculateShadowCascadeIndex(depth, numCascades, cascadeSplits);
    int infoIndex = numCascades * light.shadowViewInfoIndex + cascadeIndex;

    StructuredBuffer<uint4> clipmapInfosRaw = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodClipmapInfo)];
    Texture2DArray<uint> pageTable = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodPageTable)];
    Texture2D<uint> physicalPages = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Shadows::CLodPhysicalPages)];

    uint4 clipmapInfo0 = clipmapInfosRaw[cascadeIndex * 3 + 0];
    uint4 clipmapInfo1 = clipmapInfosRaw[cascadeIndex * 3 + 1];
    uint shadowCameraBufferIndex = clipmapInfo1.w;
    if (shadowCameraBufferIndex == kInvalidShadowCameraIndex) {
        return 0.0;
    }

    Camera lightCamera = cameraBuffer[cascadeCameraIndexBuffer[infoIndex]];
    float4 fragPosLightSpace = mul(float4(fragPosWorldSpace, 1.0), lightCamera.viewProjection);
    float3 uv = fragPosLightSpace.xyz / fragPosLightSpace.w;
    uv.xy = uv.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || uv.z < 0.0 || uv.z > 1.0) {
        return 0.0;
    }

    const uint pageTableResolution = kCLodVirtualShadowPageTableResolution;
    const uint pageX = min((uint)(uv.x * pageTableResolution), pageTableResolution - 1u);
    const uint pageY = min((uint)(uv.y * pageTableResolution), pageTableResolution - 1u);
    const uint pageTableLayer = clipmapInfo1.z;

    const uint pageEntry = pageTable.Load(int4(pageX, pageY, pageTableLayer, 0));
    if ((pageEntry & (kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask)) != (kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask)) {
        return 0.0;
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
        return 0.0;
    }

    const float closestDepth = asfloat(storedDepthBits);
    const float bias = 0.0008;
    return uv.z - bias > closestDepth ? 1.0 : 0.0;
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