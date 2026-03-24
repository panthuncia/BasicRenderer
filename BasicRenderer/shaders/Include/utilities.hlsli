#ifndef __UTILITY_HLSL__
#define __UTILITY_HLSL__
#include "include/structs.hlsli"
#include "include/cbuffers.hlsli"
#include "include/vertex.hlsli"
#include "include/materialFlags.hlsli"
#include "include/parallax.hlsli"
#include "include/gammaCorrection.hlsli"
#include "include/constants.hlsli"
#include "include/dynamicSwizzle.hlsli"

// Basic blinn-phong for uint visualization
float4 lightUints(uint meshletIndex, float3 normal, float3 viewDir) {
    float ambientIntensity = 0.3;
    float3 lightColor = float3(1, 1, 1);
    float3 lightDir = -normalize(float3(1, -1, 1));

    float3 diffuseColor = float3(
            float(meshletIndex & 1),
            float(meshletIndex & 3) / 4,
            float(meshletIndex & 7) / 8);
   float shininess = 16.0;
    
    float cosAngle = saturate(dot(normal, lightDir));
    float3 halfAngle = normalize(lightDir + viewDir);

    float blinnTerm = saturate(dot(normal, halfAngle));
    blinnTerm = cosAngle != 0.0 ? blinnTerm : 0.0;
    blinnTerm = pow(blinnTerm, shininess);

    float3 finalColor = (cosAngle + blinnTerm + ambientIntensity) * diffuseColor;

    return float4(finalColor, 1);
}

// https://johnwhite3d.blogspot.com/2017/10/signed-octahedron-normal-encoding.html
#define FLT_MAX 3.402823466e+38f
float3 SignedOctEncode(float3 n) {
    float3 OutN;

    n /= (abs(n.x) + abs(n.y) + abs(n.z));

    OutN.y = n.y * 0.5 + 0.5;
    OutN.x = n.x * 0.5 + OutN.y;
    OutN.y = n.x * -0.5 + OutN.y;

    OutN.z = saturate(n.z * FLT_MAX);
    return OutN;
}

float3 SignedOctDecode(float3 n) {
    float3 OutN;

    OutN.x = (n.x - n.y);
    OutN.y = (n.x + n.y) - 1.0;
    OutN.z = n.z * 2.0 - 1.0;
    OutN.z = OutN.z * (1.0 - abs(OutN.x) - abs(OutN.y));
 
    OutN = normalize(OutN);
    return OutN;
}

float3 computeDiffuseColor(const float3 baseColor, float metallic){
    return baseColor.rgb * (1.0 - metallic);
}

//http://www.thetenthplanet.de/archives/1180
float3x3 cotangent_frame(float3 N, float3 p, float2 uv)
{
    // get edge vectors of the pixel triangle 
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    // solve the linear system 
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    // construct a scale-invariant frame 
    float invmax = rsqrt(max(dot(T, T), dot(B, B)));
    return float3x3(T * invmax, B * invmax, N);
}

// Build cotangent frame using explicit derivatives (no ddx/ddy)
float3x3 cotangent_frame_from_derivs(
    float3 N,
    float3 dpdx, float3 dpdy,
    float2 dUVdx, float2 dUVdy)
{
    float3 dp2perp = cross(dpdy, N);
    float3 dp1perp = cross(N, dpdx);

    float3 T = dp2perp * dUVdx.x + dp1perp * dUVdy.x;
    float3 B = dp2perp * dUVdx.y + dp1perp * dUVdy.y;

    float invmax = rsqrt(max(dot(T, T), dot(B, B)));
    return float3x3(T * invmax, B * invmax, N);
}

void TestAlpha(in float2 texcoords, in uint materialDataIndex)
{
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    MaterialInfo materialInfo = materialDataBuffer[materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
        
    float4 baseColor = materialInfo.baseColorFactor;

    if (materialFlags & MATERIAL_BASE_COLOR_TEXTURE)
    {
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[materialInfo.baseColorTextureIndex];
        SamplerState baseColorSamplerState = SamplerDescriptorHeap[materialInfo.baseColorSamplerIndex];
        float4 sampledColor = baseColorTexture.Sample(baseColorSamplerState, texcoords);
#if defined(PSO_ALPHA_TEST) || defined (PSO_BLEND)
        if (baseColor.a * sampledColor.a < materialInfo.alphaCutoff){
            discard;
        }
#endif // PSO_ALPHA_TEST || PSO_BLEND
    }
    
    if (materialFlags & MATERIAL_OPACITY_TEXTURE)
    {
        Texture2D<float4> opacityTexture = ResourceDescriptorHeap[materialInfo.opacityTextureIndex];
        SamplerState opacitySamplerState = SamplerDescriptorHeap[materialInfo.opacitySamplerIndex];
        float4 opacitySample = opacityTexture.Sample(opacitySamplerState, texcoords);
        float opacity = opacitySample.a;
        baseColor.a *= opacity;
        if (baseColor.a < materialInfo.alphaCutoff)
        {
            discard;
        }
    }
}

void SampleMaterialCorePrecompiled(
    in float2 uv,
    in float2 dUVdx,
    in float2 dUVdy,
    in float3 normalWSBase,
    in float3 posWS,
    in MaterialInfo materialInfo,
    in uint materialFlags,
    out MaterialInputs ret);

void SampleMaterialCore(
    in float2 uv,
    in float3 normalWSBase,
    in float3 posWS,
    in MaterialInfo materialInfo,
    in uint materialFlags,
    out MaterialInputs ret)
{
    SampleMaterialCorePrecompiled(uv, ddx(uv), ddy(uv), normalWSBase, posWS, materialInfo, materialFlags, ret);
}

float4 Sample2DGrad(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 dUVdx, float2 dUVdy)
{
    return tex.SampleGrad(samp, uv, dUVdx, dUVdy);
}
float Sample2DGrad(Texture2D<float> tex, SamplerState samp, float2 uv, float2 dUVdx, float2 dUVdy)
{
    return tex.SampleGrad(samp, uv, dUVdx, dUVdy);
}

#define MATERIAL_MAX_UNIQUE_UV_SETS 4u
#define MATERIAL_INVALID_UV_CACHE_INDEX 0xffffffffu

enum MaterialTextureSlot
{
    MATERIAL_TEXTURE_SLOT_BASE_COLOR = 0,
    MATERIAL_TEXTURE_SLOT_OPACITY = 1,
    MATERIAL_TEXTURE_SLOT_METALLIC = 2,
    MATERIAL_TEXTURE_SLOT_ROUGHNESS = 3,
    MATERIAL_TEXTURE_SLOT_NORMAL = 4,
    MATERIAL_TEXTURE_SLOT_AO = 5,
    MATERIAL_TEXTURE_SLOT_EMISSIVE = 6,
    MATERIAL_TEXTURE_SLOT_HEIGHT = 7,
    MATERIAL_TEXTURE_SLOT_COUNT = 8
};

struct MaterialUvSample
{
    uint uvSetIndex;
    float2 uv;
    float2 dUVdx;
    float2 dUVdy;
};

struct MaterialUvCache
{
    uint count;
    MaterialUvSample samples[MATERIAL_MAX_UNIQUE_UV_SETS];
};

struct MaterialUvBindings
{
    uint cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_COUNT];
    uint tbnCacheIndex;
    uint heightCacheIndex;
    bool hasTbnSource;
    bool hasHeightSource;
};

MaterialUvSample MakeDefaultMaterialUvSample()
{
    MaterialUvSample sample = (MaterialUvSample)0;
    sample.uvSetIndex = 0u;
    return sample;
}

MaterialUvCache BuildSingleUvCache(float2 uv, float2 dUVdx, float2 dUVdy)
{
    MaterialUvCache cache = (MaterialUvCache)0;
    cache.count = 1u;
    cache.samples[0].uvSetIndex = 0u;
    cache.samples[0].uv = uv;
    cache.samples[0].dUVdx = dUVdx;
    cache.samples[0].dUVdy = dUVdy;
    return cache;
}

bool MaterialSlotEnabled(MaterialInfo materialInfo, uint materialFlags, MaterialTextureSlot slot)
{
    switch (slot)
    {
    case MATERIAL_TEXTURE_SLOT_BASE_COLOR:
        return (materialFlags & MATERIAL_BASE_COLOR_TEXTURE) != 0u;
    case MATERIAL_TEXTURE_SLOT_OPACITY:
        return (materialFlags & MATERIAL_OPACITY_TEXTURE) != 0u;
    case MATERIAL_TEXTURE_SLOT_METALLIC:
    case MATERIAL_TEXTURE_SLOT_ROUGHNESS:
        return (materialFlags & MATERIAL_PBR_MAPS) != 0u;
    case MATERIAL_TEXTURE_SLOT_NORMAL:
        return (materialFlags & MATERIAL_NORMAL_MAP) != 0u;
    case MATERIAL_TEXTURE_SLOT_AO:
        return (materialFlags & MATERIAL_AO_TEXTURE) != 0u;
    case MATERIAL_TEXTURE_SLOT_EMISSIVE:
        return (materialFlags & MATERIAL_EMISSIVE_TEXTURE) != 0u;
    case MATERIAL_TEXTURE_SLOT_HEIGHT:
        return (materialFlags & MATERIAL_PARALLAX) != 0u;
    default:
        return false;
    }
}

uint MaterialSlotUvSetIndex(MaterialInfo materialInfo, MaterialTextureSlot slot)
{
    switch (slot)
    {
    case MATERIAL_TEXTURE_SLOT_BASE_COLOR:
        return materialInfo.baseColorUvSetIndex;
    case MATERIAL_TEXTURE_SLOT_OPACITY:
        return materialInfo.opacityUvSetIndex;
    case MATERIAL_TEXTURE_SLOT_METALLIC:
        return materialInfo.metallicUvSetIndex;
    case MATERIAL_TEXTURE_SLOT_ROUGHNESS:
        return materialInfo.roughnessUvSetIndex;
    case MATERIAL_TEXTURE_SLOT_NORMAL:
        return materialInfo.normalUvSetIndex;
    case MATERIAL_TEXTURE_SLOT_AO:
        return materialInfo.aoUvSetIndex;
    case MATERIAL_TEXTURE_SLOT_EMISSIVE:
        return materialInfo.emissiveUvSetIndex;
    case MATERIAL_TEXTURE_SLOT_HEIGHT:
        return materialInfo.heightUvSetIndex;
    default:
        return 0u;
    }
}

uint FindMaterialUvCacheIndex(in MaterialUvCache cache, uint uvSetIndex)
{
    [unroll]
    for (uint i = 0u; i < MATERIAL_MAX_UNIQUE_UV_SETS; ++i)
    {
        if (i >= cache.count)
        {
            break;
        }
        if (cache.samples[i].uvSetIndex == uvSetIndex)
        {
            return i;
        }
    }

    return MATERIAL_INVALID_UV_CACHE_INDEX;
}

MaterialUvBindings BuildMaterialUvBindings(MaterialInfo materialInfo, uint materialFlags, in MaterialUvCache cache)
{
    MaterialUvBindings bindings = (MaterialUvBindings)0;

    [unroll]
    for (uint slot = 0u; slot < MATERIAL_TEXTURE_SLOT_COUNT; ++slot)
    {
        bindings.cacheIndexBySlot[slot] = MATERIAL_INVALID_UV_CACHE_INDEX;
    }

    uint uv0CacheIndex = FindMaterialUvCacheIndex(cache, 0u);

    [unroll]
    for (uint slot = 0u; slot < MATERIAL_TEXTURE_SLOT_COUNT; ++slot)
    {
        MaterialTextureSlot textureSlot = (MaterialTextureSlot)slot;
        if (!MaterialSlotEnabled(materialInfo, materialFlags, textureSlot))
        {
            continue;
        }

        uint cacheIndex = FindMaterialUvCacheIndex(cache, MaterialSlotUvSetIndex(materialInfo, textureSlot));
        if (cacheIndex == MATERIAL_INVALID_UV_CACHE_INDEX)
        {
            cacheIndex = uv0CacheIndex;
        }

        bindings.cacheIndexBySlot[slot] = cacheIndex;
    }

    bindings.tbnCacheIndex = MATERIAL_INVALID_UV_CACHE_INDEX;
    bindings.heightCacheIndex = MATERIAL_INVALID_UV_CACHE_INDEX;
    bindings.hasTbnSource = false;
    bindings.hasHeightSource = false;

    if ((materialFlags & MATERIAL_NORMAL_MAP) != 0u)
    {
        bindings.tbnCacheIndex = bindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_NORMAL];
        bindings.hasTbnSource = bindings.tbnCacheIndex != MATERIAL_INVALID_UV_CACHE_INDEX;
    }
    else if ((materialFlags & MATERIAL_PARALLAX) != 0u)
    {
        bindings.tbnCacheIndex = bindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_HEIGHT];
        bindings.hasTbnSource = bindings.tbnCacheIndex != MATERIAL_INVALID_UV_CACHE_INDEX;
    }

    if ((materialFlags & MATERIAL_PARALLAX) != 0u)
    {
        bindings.heightCacheIndex = bindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_HEIGHT];
        bindings.hasHeightSource = bindings.heightCacheIndex != MATERIAL_INVALID_UV_CACHE_INDEX;
    }

    return bindings;
}

MaterialUvSample GetBoundUvSample(in MaterialUvCache cache, in MaterialUvBindings bindings, MaterialTextureSlot slot)
{
    uint cacheIndex = bindings.cacheIndexBySlot[slot];
    if (cacheIndex == MATERIAL_INVALID_UV_CACHE_INDEX || cacheIndex >= cache.count)
    {
        return MakeDefaultMaterialUvSample();
    }

    return cache.samples[cacheIndex];
}

void SampleMaterialFromUvCache(
    in MaterialUvCache uvCache,
    in MaterialUvBindings uvBindings,
    in float3 normalWSBase,
    in float3 posWS,
    in float3 vertexColorMultiplier,
    in MaterialInfo materialInfo,
    in uint materialFlags,
    in float3 dpdx,
    in float3 dpdy,
    out MaterialInputs ret)
{
    MaterialUvSample baseColorUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_BASE_COLOR);
    MaterialUvSample opacityUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_OPACITY);
    MaterialUvSample metallicUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_METALLIC);
    MaterialUvSample roughnessUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_ROUGHNESS);
    MaterialUvSample normalUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_NORMAL);
    MaterialUvSample aoUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_AO);
    MaterialUvSample emissiveUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_EMISSIVE);

    float3x3 TBN = (float3x3)0.0f;
    if (uvBindings.hasTbnSource)
    {
        MaterialUvSample tbnUv = uvCache.samples[uvBindings.tbnCacheIndex];
        TBN = cotangent_frame_from_derivs(normalWSBase.xyz, dpdx, dpdy, tbnUv.dUVdx, tbnUv.dUVdy);
    }

    float2 parallaxUv = float2(0.0f, 0.0f);
    float2 parallaxDUdx = float2(0.0f, 0.0f);
    float2 parallaxDUdy = float2(0.0f, 0.0f);
    bool hasParallaxResolvedUv = false;

#if defined(PSO_PARALLAX)
    if (uvBindings.hasHeightSource && uvBindings.hasTbnSource)
    {
        MaterialUvSample heightUv = uvCache.samples[uvBindings.heightCacheIndex];
        ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
        StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
        Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];

        float3 viewDir = normalize(mainCamera.positionWorldSpace.xyz - posWS.xyz);

        Texture2D<float> parallaxTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.heightMapIndex)];
        SamplerState parallaxSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.heightSamplerIndex)];

        float3 uvh = getContactRefinementParallaxCoordsAndHeight(
            parallaxTexture,
            parallaxSamplerState,
            TBN,
            heightUv.uv,
            viewDir,
            materialInfo.heightMapScale);

        parallaxUv = uvh.xy;
        parallaxDUdx = heightUv.dUVdx;
        parallaxDUdy = heightUv.dUVdy;
        hasParallaxResolvedUv = true;
    }
#endif

    float4 baseColor = materialInfo.baseColorFactor;

#if defined(PSO_BASE_COLOR_TEXTURE)
    Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.baseColorTextureIndex)];
    SamplerState baseColorSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.baseColorSamplerIndex)];
    float2 baseColorSampleUv = baseColorUv.uv;
    float2 baseColorDUdx = baseColorUv.dUVdx;
    float2 baseColorDUdy = baseColorUv.dUVdy;
    if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_BASE_COLOR] == uvBindings.heightCacheIndex)
    {
        baseColorSampleUv = parallaxUv;
        baseColorDUdx = parallaxDUdx;
        baseColorDUdy = parallaxDUdy;
    }
    float4 sampledColor = Sample2DGrad(baseColorTexture, baseColorSamplerState, baseColorSampleUv, baseColorDUdx, baseColorDUdy);
    baseColor *= sampledColor;
#endif

#if defined(PSO_OPACITY_TEXTURE)
    Texture2D<float4> opacityTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.opacityTextureIndex)];
    SamplerState opacitySamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.opacitySamplerIndex)];
    float2 opacitySampleUv = opacityUv.uv;
    float2 opacityDUdx = opacityUv.dUVdx;
    float2 opacityDUdy = opacityUv.dUVdy;
    if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_OPACITY] == uvBindings.heightCacheIndex)
    {
        opacitySampleUv = parallaxUv;
        opacityDUdx = parallaxDUdx;
        opacityDUdy = parallaxDUdy;
    }
    float4 opacitySample = Sample2DGrad(opacityTexture, opacitySamplerState, opacitySampleUv, opacityDUdx, opacityDUdy);
    baseColor.a *= opacitySample.a;
#endif

    float metallic = 0.0f;
    float roughness = 0.0f;

#if defined(PSO_PBR_MAPS)
    Texture2D<float4> metallicTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicTextureIndex)];
    SamplerState metallicSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicSamplerIndex)];
    Texture2D<float4> roughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessTextureIndex)];
    SamplerState roughnessSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessSamplerIndex)];

    float2 metallicSampleUv = metallicUv.uv;
    float2 metallicDUdx = metallicUv.dUVdx;
    float2 metallicDUdy = metallicUv.dUVdy;
    if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_METALLIC] == uvBindings.heightCacheIndex)
    {
        metallicSampleUv = parallaxUv;
        metallicDUdx = parallaxDUdx;
        metallicDUdy = parallaxDUdy;
    }

    float2 roughnessSampleUv = roughnessUv.uv;
    float2 roughnessDUdx = roughnessUv.dUVdx;
    float2 roughnessDUdy = roughnessUv.dUVdy;
    if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_ROUGHNESS] == uvBindings.heightCacheIndex)
    {
        roughnessSampleUv = parallaxUv;
        roughnessDUdx = parallaxDUdx;
        roughnessDUdy = parallaxDUdy;
    }

    float4 metallicSample = Sample2DGrad(metallicTexture, metallicSamplerState, metallicSampleUv, metallicDUdx, metallicDUdy);
    float4 roughnessSample = Sample2DGrad(roughnessTexture, roughnessSamplerState, roughnessSampleUv, roughnessDUdx, roughnessDUdy);

    metallic = DynamicSwizzle(metallicSample, materialInfo.metallicChannel) * materialInfo.metallicFactor;
    roughness = DynamicSwizzle(roughnessSample, materialInfo.roughnessChannel) * materialInfo.roughnessFactor;
#else
    metallic = materialInfo.metallicFactor;
    roughness = materialInfo.roughnessFactor;
#endif

    float3 normalWS = normalWSBase;

#if defined(PSO_NORMAL_MAP)
    if (uvBindings.hasTbnSource)
    {
        Texture2D<float4> normalTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.normalTextureIndex)];
        SamplerState normalSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.normalSamplerIndex)];
        float2 normalSampleUv = normalUv.uv;
        float2 normalDUdx = normalUv.dUVdx;
        float2 normalDUdy = normalUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_NORMAL] == uvBindings.heightCacheIndex)
        {
            normalSampleUv = parallaxUv;
            normalDUdx = parallaxDUdx;
            normalDUdy = parallaxDUdy;
        }

        float3 textureNormal = Sample2DGrad(normalTexture, normalSamplerState, normalSampleUv, normalDUdx, normalDUdy).rgb;
        float3 tangentSpaceNormal = normalize(textureNormal * 2.0 - 1.0);

        if (materialFlags & MATERIAL_NEGATE_NORMALS) tangentSpaceNormal = -tangentSpaceNormal;
        if (materialFlags & MATERIAL_INVERT_NORMAL_GREEN) tangentSpaceNormal.g = -tangentSpaceNormal.g;

        normalWS = normalize(mul(tangentSpaceNormal, TBN));
    }
#endif

    float ao = 1.0f;
#if defined(PSO_AO_TEXTURE)
    Texture2D<float4> aoTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.aoMapIndex)];
    SamplerState aoSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.aoSamplerIndex)];
    float2 aoSampleUv = aoUv.uv;
    float2 aoDUdx = aoUv.dUVdx;
    float2 aoDUdy = aoUv.dUVdy;
    if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_AO] == uvBindings.heightCacheIndex)
    {
        aoSampleUv = parallaxUv;
        aoDUdx = parallaxDUdx;
        aoDUdy = parallaxDUdy;
    }
    ao = Sample2DGrad(aoTexture, aoSamplerState, aoSampleUv, aoDUdx, aoDUdy).r;
#endif

    float3 emissive = materialInfo.emissiveFactor.rgb;
#if defined(PSO_EMISSIVE_TEXTURE)
    Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.emissiveTextureIndex)];
    SamplerState emissiveSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.emissiveSamplerIndex)];
    float2 emissiveSampleUv = emissiveUv.uv;
    float2 emissiveDUdx = emissiveUv.dUVdx;
    float2 emissiveDUdy = emissiveUv.dUVdy;
    if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_EMISSIVE] == uvBindings.heightCacheIndex)
    {
        emissiveSampleUv = parallaxUv;
        emissiveDUdx = parallaxDUdx;
        emissiveDUdy = parallaxDUdy;
    }
    float4 emissiveSample = Sample2DGrad(emissiveTexture, emissiveSamplerState, emissiveSampleUv, emissiveDUdx, emissiveDUdy);
    emissive = float3(
        DynamicSwizzle(emissiveSample, materialInfo.emissiveChannels.x),
        DynamicSwizzle(emissiveSample, materialInfo.emissiveChannels.y),
        DynamicSwizzle(emissiveSample, materialInfo.emissiveChannels.z)) * materialInfo.emissiveFactor.rgb;
#endif

    ret.albedo = baseColor.rgb * vertexColorMultiplier;
    ret.normalWS = normalWS;
    ret.emissive = emissive;
    ret.metallic = metallic;
    ret.roughness = roughness;
    ret.opacity = baseColor.a;
    ret.ambientOcclusion = ao;
}

void SampleMaterialFromUvCacheRuntime(
    in MaterialUvCache uvCache,
    in MaterialUvBindings uvBindings,
    in float3 normalWSBase,
    in float3 posWS,
    in float3 vertexColorMultiplier,
    in MaterialInfo materialInfo,
    in uint materialFlags,
    in float3 dpdx,
    in float3 dpdy,
    out MaterialInputs ret)
{
    MaterialUvSample baseColorUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_BASE_COLOR);
    MaterialUvSample opacityUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_OPACITY);
    MaterialUvSample metallicUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_METALLIC);
    MaterialUvSample roughnessUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_ROUGHNESS);
    MaterialUvSample normalUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_NORMAL);
    MaterialUvSample aoUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_AO);
    MaterialUvSample emissiveUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_EMISSIVE);

    float3x3 TBN = (float3x3)0.0f;
    if (uvBindings.hasTbnSource)
    {
        MaterialUvSample tbnUv = uvCache.samples[uvBindings.tbnCacheIndex];
        TBN = cotangent_frame_from_derivs(normalWSBase.xyz, dpdx, dpdy, tbnUv.dUVdx, tbnUv.dUVdy);
    }

    float2 parallaxUv = float2(0.0f, 0.0f);
    float2 parallaxDUdx = float2(0.0f, 0.0f);
    float2 parallaxDUdy = float2(0.0f, 0.0f);
    bool hasParallaxResolvedUv = false;

    if ((materialFlags & MATERIAL_PARALLAX) != 0u && uvBindings.hasHeightSource && uvBindings.hasTbnSource)
    {
        MaterialUvSample heightUv = uvCache.samples[uvBindings.heightCacheIndex];
        ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
        StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
        Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];

        float3 viewDir = normalize(mainCamera.positionWorldSpace.xyz - posWS);

        Texture2D<float> parallaxTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.heightMapIndex)];
        SamplerState parallaxSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.heightSamplerIndex)];

        float3 uvh = getContactRefinementParallaxCoordsAndHeight(
            parallaxTexture,
            parallaxSamplerState,
            TBN,
            heightUv.uv,
            viewDir,
            materialInfo.heightMapScale);

        parallaxUv = uvh.xy;
        parallaxDUdx = heightUv.dUVdx;
        parallaxDUdy = heightUv.dUVdy;
        hasParallaxResolvedUv = true;
    }

    float4 baseColor = materialInfo.baseColorFactor;

    if ((materialFlags & MATERIAL_BASE_COLOR_TEXTURE) != 0u)
    {
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.baseColorTextureIndex)];
        SamplerState baseColorSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.baseColorSamplerIndex)];
        float2 baseColorSampleUv = baseColorUv.uv;
        float2 baseColorDUdx = baseColorUv.dUVdx;
        float2 baseColorDUdy = baseColorUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_BASE_COLOR] == uvBindings.heightCacheIndex)
        {
            baseColorSampleUv = parallaxUv;
            baseColorDUdx = parallaxDUdx;
            baseColorDUdy = parallaxDUdy;
        }
        float4 sampledColor = Sample2DGrad(baseColorTexture, baseColorSamplerState, baseColorSampleUv, baseColorDUdx, baseColorDUdy);
        baseColor *= sampledColor;
    }

    if ((materialFlags & MATERIAL_OPACITY_TEXTURE) != 0u)
    {
        Texture2D<float4> opacityTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.opacityTextureIndex)];
        SamplerState opacitySamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.opacitySamplerIndex)];
        float2 opacitySampleUv = opacityUv.uv;
        float2 opacityDUdx = opacityUv.dUVdx;
        float2 opacityDUdy = opacityUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_OPACITY] == uvBindings.heightCacheIndex)
        {
            opacitySampleUv = parallaxUv;
            opacityDUdx = parallaxDUdx;
            opacityDUdy = parallaxDUdy;
        }
        float4 opacitySample = Sample2DGrad(opacityTexture, opacitySamplerState, opacitySampleUv, opacityDUdx, opacityDUdy);
        baseColor.a *= opacitySample.a;
    }

    float metallic = materialInfo.metallicFactor;
    float roughness = materialInfo.roughnessFactor;
    if ((materialFlags & MATERIAL_PBR_MAPS) != 0u)
    {
        Texture2D<float4> metallicTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicTextureIndex)];
        SamplerState metallicSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicSamplerIndex)];
        Texture2D<float4> roughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessTextureIndex)];
        SamplerState roughnessSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessSamplerIndex)];

        float2 metallicSampleUv = metallicUv.uv;
        float2 metallicDUdx = metallicUv.dUVdx;
        float2 metallicDUdy = metallicUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_METALLIC] == uvBindings.heightCacheIndex)
        {
            metallicSampleUv = parallaxUv;
            metallicDUdx = parallaxDUdx;
            metallicDUdy = parallaxDUdy;
        }

        float2 roughnessSampleUv = roughnessUv.uv;
        float2 roughnessDUdx = roughnessUv.dUVdx;
        float2 roughnessDUdy = roughnessUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_ROUGHNESS] == uvBindings.heightCacheIndex)
        {
            roughnessSampleUv = parallaxUv;
            roughnessDUdx = parallaxDUdx;
            roughnessDUdy = parallaxDUdy;
        }

        float4 metallicSample = Sample2DGrad(metallicTexture, metallicSamplerState, metallicSampleUv, metallicDUdx, metallicDUdy);
        float4 roughnessSample = Sample2DGrad(roughnessTexture, roughnessSamplerState, roughnessSampleUv, roughnessDUdx, roughnessDUdy);

        metallic = DynamicSwizzle(metallicSample, materialInfo.metallicChannel) * materialInfo.metallicFactor;
        roughness = DynamicSwizzle(roughnessSample, materialInfo.roughnessChannel) * materialInfo.roughnessFactor;
    }

    float3 normalWS = normalWSBase;
    if ((materialFlags & MATERIAL_NORMAL_MAP) != 0u && uvBindings.hasTbnSource)
    {
        Texture2D<float4> normalTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.normalTextureIndex)];
        SamplerState normalSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.normalSamplerIndex)];
        float2 normalSampleUv = normalUv.uv;
        float2 normalDUdx = normalUv.dUVdx;
        float2 normalDUdy = normalUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_NORMAL] == uvBindings.heightCacheIndex)
        {
            normalSampleUv = parallaxUv;
            normalDUdx = parallaxDUdx;
            normalDUdy = parallaxDUdy;
        }

        float3 textureNormal = Sample2DGrad(normalTexture, normalSamplerState, normalSampleUv, normalDUdx, normalDUdy).rgb;
        float3 tangentSpaceNormal = normalize(textureNormal * 2.0 - 1.0);

        if ((materialFlags & MATERIAL_NEGATE_NORMALS) != 0u) tangentSpaceNormal = -tangentSpaceNormal;
        if ((materialFlags & MATERIAL_INVERT_NORMAL_GREEN) != 0u) tangentSpaceNormal.g = -tangentSpaceNormal.g;

        normalWS = normalize(mul(tangentSpaceNormal, TBN));
    }

    float ao = 1.0f;
    if ((materialFlags & MATERIAL_AO_TEXTURE) != 0u)
    {
        Texture2D<float4> aoTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.aoMapIndex)];
        SamplerState aoSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.aoSamplerIndex)];
        float2 aoSampleUv = aoUv.uv;
        float2 aoDUdx = aoUv.dUVdx;
        float2 aoDUdy = aoUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_AO] == uvBindings.heightCacheIndex)
        {
            aoSampleUv = parallaxUv;
            aoDUdx = parallaxDUdx;
            aoDUdy = parallaxDUdy;
        }
        ao = Sample2DGrad(aoTexture, aoSamplerState, aoSampleUv, aoDUdx, aoDUdy).r;
    }

    float3 emissive = materialInfo.emissiveFactor.rgb;
    if ((materialFlags & MATERIAL_EMISSIVE_TEXTURE) != 0u)
    {
        Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.emissiveTextureIndex)];
        SamplerState emissiveSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.emissiveSamplerIndex)];
        float2 emissiveSampleUv = emissiveUv.uv;
        float2 emissiveDUdx = emissiveUv.dUVdx;
        float2 emissiveDUdy = emissiveUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_EMISSIVE] == uvBindings.heightCacheIndex)
        {
            emissiveSampleUv = parallaxUv;
            emissiveDUdx = parallaxDUdx;
            emissiveDUdy = parallaxDUdy;
        }
        float4 emissiveSample = Sample2DGrad(emissiveTexture, emissiveSamplerState, emissiveSampleUv, emissiveDUdx, emissiveDUdy);
        emissive = float3(
            DynamicSwizzle(emissiveSample, materialInfo.emissiveChannels.x),
            DynamicSwizzle(emissiveSample, materialInfo.emissiveChannels.y),
            DynamicSwizzle(emissiveSample, materialInfo.emissiveChannels.z)) * materialInfo.emissiveFactor.rgb;
    }

    ret.albedo = baseColor.rgb * vertexColorMultiplier;
    ret.normalWS = normalWS;
    ret.emissive = emissive;
    ret.metallic = metallic;
    ret.roughness = roughness;
    ret.opacity = baseColor.a;
    ret.ambientOcclusion = ao;
}

void SampleMaterialCorePrecompiled(
    in float2 uv,
    in float2 dUVdx,
    in float2 dUVdy,
    in float3 normalWSBase,
    in float3 posWS,
    in MaterialInfo materialInfo,
    in uint materialFlags,
    out MaterialInputs ret)
{
    MaterialUvCache uvCache = BuildSingleUvCache(uv, dUVdx, dUVdy);
    MaterialUvBindings uvBindings = BuildMaterialUvBindings(materialInfo, materialFlags, uvCache);
    SampleMaterialFromUvCache(uvCache, uvBindings, normalWSBase, posWS, float3(1.0f, 1.0f, 1.0f), materialInfo, materialFlags, ddx(posWS), ddy(posWS), ret);
    return;
#if 0
    float2 localUV = uv;
    float2 localDUdx = dUVdx;
    float2 localDUdy = dUVdy;

#if defined(PSO_PARALLAX)
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];

    float3 viewDir = normalize(mainCamera.positionWorldSpace.xyz - posWS.xyz);

    Texture2D<float> parallaxTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.heightMapIndex)];
    SamplerState parallaxSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.heightSamplerIndex)];

    // IMPORTANT: inside getContactRefinementParallaxCoordsAndHeight, use SampleGrad too.
    float3 uvh = getContactRefinementParallaxCoordsAndHeight(
        parallaxTexture,
        parallaxSamplerState,
        TBN,
        localUV,
        viewDir,
        materialInfo.heightMapScale,
        localDUdx,
        localDUdy
    );

    localUV = uvh.xy;

    // If you *don't* implement derivative correction for parallax, keeping the original gradients
    // is the common approximation (it's usually fine).
#endif

    // Base color
    float4 baseColor = materialInfo.baseColorFactor;

#if defined(PSO_BASE_COLOR_TEXTURE)
    Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.baseColorTextureIndex)];
    SamplerState baseColorSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.baseColorSamplerIndex)];
    float4 sampledColor = Sample2DGrad(baseColorTexture, baseColorSamplerState, localUV, localDUdx, localDUdy);
    baseColor *= sampledColor;
#endif

#if defined(PSO_OPACITY_TEXTURE)
    Texture2D<float4> opacityTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.opacityTextureIndex)];
    SamplerState opacitySamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.opacitySamplerIndex)];
    float4 opacitySample = Sample2DGrad(opacityTexture, opacitySamplerState, localUV, localDUdx, localDUdy);
    baseColor.a *= opacitySample.a;
#endif

    // Metallic / roughness
    float metallic = 0.0;
    float roughness = 0.0;

#if defined(PSO_PBR_MAPS)
    Texture2D<float4> metallicTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicTextureIndex)];
    SamplerState metallicSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicSamplerIndex)];
    Texture2D<float4> roughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessTextureIndex)];
    SamplerState roughnessSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessSamplerIndex)];

    float4 metallicSample  = Sample2DGrad(metallicTexture,  metallicSamplerState,  localUV, localDUdx, localDUdy);
    float4 roughnessSample = Sample2DGrad(roughnessTexture, roughnessSamplerState, localUV, localDUdx, localDUdy);

    metallic  = DynamicSwizzle(metallicSample,  materialInfo.metallicChannel)  * materialInfo.metallicFactor;
    roughness = DynamicSwizzle(roughnessSample, materialInfo.roughnessChannel) * materialInfo.roughnessFactor;
#else
    metallic = materialInfo.metallicFactor;
    roughness = materialInfo.roughnessFactor;
#endif

    // Normal map
    float3 normalWS = normalWSBase;

#if defined(PSO_NORMAL_MAP)
    Texture2D<float4> normalTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.normalTextureIndex)];
    SamplerState normalSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.normalSamplerIndex)];

    float3 textureNormal = Sample2DGrad(normalTexture, normalSamplerState, localUV, localDUdx, localDUdy).rgb;
    float3 tangentSpaceNormal = normalize(textureNormal * 2.0 - 1.0);

    if (materialFlags & MATERIAL_NEGATE_NORMALS)       tangentSpaceNormal = -tangentSpaceNormal;
    if (materialFlags & MATERIAL_INVERT_NORMAL_GREEN)  tangentSpaceNormal.g = -tangentSpaceNormal.g;

    normalWS = normalize(mul(tangentSpaceNormal, TBN));
#endif

    // AO
    float ao = 1.0;
#if defined(PSO_AO_TEXTURE)
    Texture2D<float4> aoTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.aoMapIndex)];
    SamplerState aoSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.aoSamplerIndex)];
    ao = Sample2DGrad(aoTexture, aoSamplerState, localUV, localDUdx, localDUdy).r;
#endif

    // Emissive
    float3 emissive = materialInfo.emissiveFactor.rgb;
#if defined(PSO_EMISSIVE_TEXTURE)
    Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.emissiveTextureIndex)];
    SamplerState emissiveSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.emissiveSamplerIndex)];
    emissive = Sample2DGrad(emissiveTexture, emissiveSamplerState, localUV, localDUdx, localDUdy).rgb * materialInfo.emissiveFactor.rgb;
#endif

    ret.albedo = baseColor.rgb;
    ret.normalWS = normalWS;
    ret.emissive = emissive;
    ret.metallic = metallic;
    ret.roughness = roughness;
    ret.opacity = baseColor.a;
    ret.ambientOcclusion = ao;
#endif
}

void SampleMaterial(
    in float2 uv,
    in float3 normalWSBase,
    in float3 posWS,
    in uint materialDataIndex,
    out MaterialInputs ret)
{
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    MaterialInfo materialInfo = materialDataBuffer[materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
    SampleMaterialCore(uv, normalWSBase, posWS, materialInfo, materialFlags, ret);

    // For PS version, alpha test / discard here
#if defined(PSO_ALPHA_TEST) || defined(PSO_BLEND)
    if (ret.opacity < materialInfo.alphaCutoff)
    {
        discard;
    }
#endif
}

void SampleMaterialPrecompiled(
    in float2 uv,
    in float3 normalWSBase,
    in float3 posWS,
    in uint materialDataIndex,
    out MaterialInputs ret)
{
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    MaterialInfo materialInfo = materialDataBuffer[materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
    SampleMaterialCorePrecompiled(uv, ddx(uv), ddy(uv), normalWSBase, posWS, materialInfo, materialFlags, ret);

    // For PS version, alpha test / discard here
#if defined(PSO_ALPHA_TEST) || defined(PSO_BLEND)
    if (ret.opacity < materialInfo.alphaCutoff)
    {
        discard;
    }
#endif
}

void GetMaterialInfoForFragment(in const PSInput input, out MaterialInputs ret)
{
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    uint meshBufferIndexLocal = perMeshBufferIndex;
    PerMeshBuffer meshBuffer = perMeshBuffer[meshBufferIndexLocal];
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    MaterialInfo materialInfo = materialDataBuffer[meshBuffer.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
    MaterialUvCache uvCache = BuildSingleUvCache(input.texcoord, ddx(input.texcoord), ddy(input.texcoord));
    MaterialUvBindings uvBindings = BuildMaterialUvBindings(materialInfo, materialFlags, uvCache);
    SampleMaterialFromUvCache(uvCache, uvBindings, input.normalWorldSpace, input.positionWorldSpace.xyz, input.color, materialInfo, materialFlags, ddx(input.positionWorldSpace.xyz), ddy(input.positionWorldSpace.xyz), ret);
}

void GetMaterialInfoForFragmentPrecompiled(in const PSInput input, out MaterialInputs ret)
{
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    uint meshBufferIndexLocal = perMeshBufferIndex;
    PerMeshBuffer meshBuffer = perMeshBuffer[meshBufferIndexLocal];
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    MaterialInfo materialInfo = materialDataBuffer[meshBuffer.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
    MaterialUvCache uvCache = BuildSingleUvCache(input.texcoord, ddx(input.texcoord), ddy(input.texcoord));
    MaterialUvBindings uvBindings = BuildMaterialUvBindings(materialInfo, materialFlags, uvCache);
    SampleMaterialFromUvCache(
        uvCache,
        uvBindings,
        input.normalWorldSpace,
        input.positionWorldSpace.xyz,
        input.color,
        materialInfo,
        materialFlags,
        ddx(input.positionWorldSpace.xyz),
        ddy(input.positionWorldSpace.xyz),
        ret);
}

void SampleMaterialCS(
    in float2 uv,
    in float3 normalWSBase,
    in float3 posWS,
    in uint materialDataIndex,
    in float3 dpdx,
    in float3 dpdy,
    in float2 dUVdx,
    in float2 dUVdy,
    out MaterialInputs ret)
{
    StructuredBuffer<MaterialInfo> materialDataBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    MaterialInfo materialInfo = materialDataBuffer[materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
    MaterialUvCache uvCache = BuildSingleUvCache(uv, dUVdx, dUVdy);
    MaterialUvBindings uvBindings = BuildMaterialUvBindings(materialInfo, materialFlags, uvCache);
    SampleMaterialFromUvCache(uvCache, uvBindings, normalWSBase, posWS, float3(1.0f, 1.0f, 1.0f), materialInfo, materialFlags, dpdx, dpdy, ret);
}

float PerceptualRoughnessToRoughness(float perceptualRoughness)
{
    return perceptualRoughness * perceptualRoughness;
}

float3 computeF0(const float4 baseColor, float metallic, float reflectance)
{
    return baseColor.rgb * metallic + (reflectance * (1.0 - metallic));
}

float computeDielectricF0(float reflectance)
{
    return 0.16 * reflectance * reflectance;
}

void GetFragmentInfoScreenSpace(in uint2 pixelCoordinates, in float3 viewWS, in float3 fragPosViewSpace, in float3 fragPosWorldSpace, in bool enableGTAO, out FragmentInfo ret) {
    ret.pixelCoords = pixelCoordinates;
    ret.fragPosViewSpace = fragPosViewSpace;
    ret.fragPosWorldSpace = fragPosWorldSpace;
    
    // Gather textures
    Texture2D<float4> normalsTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Normals)];
    
    // Load values
    ret.normalWS = normalsTexture[pixelCoordinates].xyz;
    //ret.normalWS = SignedOctDecode(encodedNormal.yzw);
    
    Texture2D<float4> albedoTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Albedo)];
    float4 baseColorSample = albedoTexture[pixelCoordinates];
    ret.albedo = baseColorSample.xyz;
    
    Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Emissive)];
    float4 emissive = emissiveTexture[pixelCoordinates];
    ret.emissive = emissive.xyz;
    
    if (enableGTAO)
    {
        Texture2D<uint> aoTexture = ResourceDescriptorHeap[OptionalResourceDescriptorIndex(Builtin::GTAO::OutputAOTerm)];
        ret.diffuseAmbientOcclusion = min(baseColorSample.w, float(aoTexture[pixelCoordinates].x) / 255.0);
    }
    else
    {
        ret.diffuseAmbientOcclusion = baseColorSample.w; // AO stored in alpha channel
    }
    
    Texture2D<float2> metallicRoughnessTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::MetallicRoughness)];
    float2 metallicRoughness = metallicRoughnessTexture[pixelCoordinates];
    
    float perceptualRoughness = metallicRoughness.y;
    ret.perceptualRoughnessUnclamped = perceptualRoughness;
    // Clamp the roughness to a minimum value to avoid divisions by 0 during lighting
    ret.perceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    // Remaps the roughness to a perceptually linear roughness (roughness^2)
    ret.roughness = PerceptualRoughnessToRoughness(ret.perceptualRoughness);
    ret.roughnessUnclamped = PerceptualRoughnessToRoughness(ret.perceptualRoughnessUnclamped);
    
    ret.viewWS = viewWS;
    //ret.NdotV = dot(ret.normalWS, viewWS);
    ret.NdotV = dot(ret.normalWS, ret.viewWS);
    ret.normalWS = normalize(ret.normalWS + max(0, -ret.NdotV + MIN_N_DOT_V) * ret.viewWS);
    ret.NdotV = max(MIN_N_DOT_V, ret.NdotV);
    ret.reflectedWS = reflect(-ret.viewWS, ret.normalWS);
    
    //ret.DFG = prefilteredDFG(ret.perceptualRoughness, ret.NdotV);
    
    ret.metallic = metallicRoughness.x;
    ret.diffuseColor = computeDiffuseColor(baseColorSample.xyz, ret.metallic);
    ret.alpha = 1.0; // Opaque objects
    
    ret.reflectance = 0.35; // This is a default value for the reflectance of dielectrics, similar to setting an F0 directly. Ideally, each material should have its own reflectance value.
    // Assumes an interface from air to an IOR of 1.5 for dielectrics
    ret.dielectricF0 = computeDielectricF0(ret.reflectance);
    ret.F0 = computeF0(float4(baseColorSample.xyz, 1.0), ret.metallic, ret.dielectricF0); // base albedo, not the diffuse color
    ret.dielectricF0 *= (1.0 - ret.metallic);
}

void FillFragmentInfoDirect(inout FragmentInfo ret, in MaterialInputs materialInfo, in float3 viewWS, in float2 pixelCoords, in bool transparent, in bool isFrontFace, in uint materialFlags)
{
    ret.materialFlags = materialFlags;
    ret.metallic = materialInfo.metallic;
    float perceptualRoughness = materialInfo.roughness;
    ret.perceptualRoughnessUnclamped = perceptualRoughness;
    // Clamp the roughness to a minimum value to avoid divisions by 0 during lighting
    ret.perceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    // Remaps the roughness to a perceptually linear roughness (roughness^2)
    ret.roughness = PerceptualRoughnessToRoughness(ret.perceptualRoughness);
    ret.roughnessUnclamped = PerceptualRoughnessToRoughness(ret.perceptualRoughnessUnclamped);

    ret.normalWS = materialInfo.normalWS;
    if (!isFrontFace && ((materialFlags & MATERIAL_DOUBLE_SIDED) != 0u)) {
        ret.normalWS = -ret.normalWS;
    }
    
    ret.viewWS = viewWS;
    //ret.NdotV = dot(ret.normalWS, viewWS);
    ret.NdotV = dot(ret.normalWS, ret.viewWS);
    ret.normalWS = normalize(ret.normalWS + max(0, -ret.NdotV + MIN_N_DOT_V) * ret.viewWS);
    ret.NdotV = max(MIN_N_DOT_V, ret.NdotV);
    
    ret.reflectedWS = reflect(-ret.viewWS, ret.normalWS);
    
    //ret.DFG = prefilteredDFG(ret.perceptualRoughness, ret.NdotV);
    
    ret.diffuseColor = computeDiffuseColor(materialInfo.albedo, ret.metallic);
    ret.albedo = materialInfo.albedo;
    if (transparent)
    {
        ret.alpha = materialInfo.opacity;
        ret.diffuseAmbientOcclusion = materialInfo.ambientOcclusion; // Screen-space AO not applied to transparent objects
    }
    else
    {
        ret.alpha = 1.0; // Opaque objects
        if (enableGTAO)
        {
            Texture2D<uint> aoTexture = ResourceDescriptorHeap[OptionalResourceDescriptorIndex(Builtin::GTAO::OutputAOTerm)];
            ret.diffuseAmbientOcclusion = min(materialInfo.ambientOcclusion, float(aoTexture[pixelCoords].x) / 255.0);
        }
        else
        {
            ret.diffuseAmbientOcclusion = materialInfo.ambientOcclusion;
        }
    }
    
    ret.reflectance = 0.35; // This is a default value for the reflectance of dielectrics, similar to setting an F0 directly. Ideally, each material should have its own reflectance value.
    // Assumes an interface from air to an IOR of 1.5 for dielectrics
    ret.dielectricF0 = computeDielectricF0(ret.reflectance);
    ret.F0 = computeF0(float4(materialInfo.albedo.xyz, 1.0), ret.metallic, ret.dielectricF0); // base albedo, not the diffuse color
    ret.dielectricF0 *= (1.0 - ret.metallic);
    
    ret.emissive = materialInfo.emissive; // TODO
}

void GetFragmentInfoDirectPrecompiled(in PSInput input, in float3 viewWS, bool enableGTAO, bool transparent, bool isFrontFace, out FragmentInfo ret)
{
    ret.pixelCoords = input.position.xy;
    ret.fragPosViewSpace = input.positionViewSpace.xyz;
    ret.fragPosWorldSpace = input.positionWorldSpace.xyz;
    
    MaterialInputs materialInfo;
    GetMaterialInfoForFragmentPrecompiled(input, materialInfo);
    
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    MaterialInfo materialData = materialDataBuffer[meshBuffer.materialDataIndex];
    FillFragmentInfoDirect(ret, materialInfo, viewWS, input.position.xy, transparent, isFrontFace, materialData.materialFlags);
}

void GetFragmentInfoDirect(in PSInput input, in float3 viewWS, bool enableGTAO, bool transparent, bool isFrontFace, out FragmentInfo ret)
{
    ret.pixelCoords = input.position.xy;
    ret.fragPosViewSpace = input.positionViewSpace.xyz;
    ret.fragPosWorldSpace = input.positionWorldSpace.xyz;
    
    MaterialInputs materialInfo;
    GetMaterialInfoForFragment(input, materialInfo);

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    MaterialInfo materialData = materialDataBuffer[meshBuffer.materialDataIndex];
    FillFragmentInfoDirect(ret, materialInfo, viewWS, input.position.xy, transparent, isFrontFace, materialData.materialFlags);
}

float unprojectDepth(float depth, float near, float far)
{
    return near * far / (far - depth * (far - near));
}

#endif // __UTILITY_HLSL__
