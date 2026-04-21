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

struct OpenPBRSurfaceSample
{
    uint openPBRMaterialDataIndex;
    float3 baseColor;
    float baseMetalness;
    float specularRoughness;
    float3 coatColor;
    float coatWeight;
    float coatRoughness;
    float3 fuzzColor;
    float fuzzWeight;
    float fuzzRoughness;
    float opacity;
    float3 emissive;
};

OpenPBRMaterialInfo LoadOpenPBRMaterialInfo(uint openPBRMaterialDataIndex)
{
    StructuredBuffer<OpenPBRMaterialInfo> openPBRMaterialBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialOpenPBRDataBuffer)];
    return openPBRMaterialBuffer[openPBRMaterialDataIndex];
}

OpenPBRMaterialInfo LoadOpenPBRMaterialInfo(MaterialInfo materialInfo)
{
    return LoadOpenPBRMaterialInfo(materialInfo.openPBRMaterialDataIndex);
}

OpenPBRSurfaceSample ResolveCanonicalOpenPBRSurface(
    MaterialInfo materialInfo,
    float3 sampledBaseColor,
    float sampledMetalness,
    float sampledSpecularRoughness,
    float sampledOpacity,
    float3 sampledEmissive)
{
    OpenPBRMaterialInfo openPBRMaterialInfo = LoadOpenPBRMaterialInfo(materialInfo);
    const float3 canonicalEmissive = openPBRMaterialInfo.emissionColor * openPBRMaterialInfo.emissionLuminance;

    OpenPBRSurfaceSample surface = (OpenPBRSurfaceSample)0;
    surface.openPBRMaterialDataIndex = materialInfo.openPBRMaterialDataIndex;
    surface.baseColor = sampledBaseColor;
    surface.baseMetalness = sampledMetalness;
    surface.specularRoughness = sampledSpecularRoughness;
    surface.coatColor = saturate(openPBRMaterialInfo.coatColor);
    surface.coatWeight = saturate(openPBRMaterialInfo.coatWeight);
    surface.coatRoughness = saturate(openPBRMaterialInfo.coatRoughness);
    surface.fuzzColor = saturate(openPBRMaterialInfo.fuzzColor);
    surface.fuzzWeight = saturate(openPBRMaterialInfo.fuzzWeight);
    surface.fuzzRoughness = saturate(openPBRMaterialInfo.fuzzRoughness);
    surface.opacity = saturate(sampledOpacity * openPBRMaterialInfo.geometryOpacity);
    surface.emissive = dot(sampledEmissive, sampledEmissive) > 0.0f ? sampledEmissive : canonicalEmissive;
    return surface;
}

void PopulateLegacyMaterialInputsFromOpenPBRSurface(
    OpenPBRSurfaceSample surface,
    float3 normalWS,
    float ambientOcclusion,
    out MaterialInputs materialInputs)
{
    materialInputs.albedo = surface.baseColor;
    materialInputs.normalWS = normalWS;
    materialInputs.emissive = surface.emissive;
    materialInputs.coatColor = surface.coatColor;
    materialInputs.metallic = surface.baseMetalness;
    materialInputs.roughness = surface.specularRoughness;
    materialInputs.coatWeight = surface.coatWeight;
    materialInputs.coatRoughness = surface.coatRoughness;
    materialInputs.fuzzColor = surface.fuzzColor;
    materialInputs.fuzzWeight = surface.fuzzWeight;
    materialInputs.fuzzRoughness = surface.fuzzRoughness;
    materialInputs.opacity = surface.opacity;
    materialInputs.ambientOcclusion = ambientOcclusion;
    materialInputs.openPBRMaterialDataIndex = surface.openPBRMaterialDataIndex;
}

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
    float2 dTexcoordsDx = ddx(texcoords);
    float2 dTexcoordsDy = ddy(texcoords);
        
    float4 baseColor = materialInfo.baseColorFactor;

    if (materialFlags & MATERIAL_BASE_COLOR_TEXTURE)
    {
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.baseColorTextureIndex)];
        SamplerState baseColorSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.baseColorSamplerIndex)];
        float4 sampledColor = baseColorTexture.SampleGrad(baseColorSamplerState, texcoords, dTexcoordsDx, dTexcoordsDy);
#if defined(PSO_ALPHA_TEST) || defined (PSO_BLEND)
        if (baseColor.a * sampledColor.a < materialInfo.alphaCutoff){
            discard;
        }
#endif // PSO_ALPHA_TEST || PSO_BLEND
    }
    
    if (materialFlags & MATERIAL_OPACITY_TEXTURE)
    {
        Texture2D<float4> opacityTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.opacityTextureIndex)];
        SamplerState opacitySamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.opacitySamplerIndex)];
        float4 opacitySample = opacityTexture.SampleGrad(opacitySamplerState, texcoords, dTexcoordsDx, dTexcoordsDy);
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

#define MATERIAL_MAX_UNIQUE_UV_SETS 8u
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

static const uint OPENPBR_INVALID_TEXTURE_INDEX = 0xffffffffu;

bool HasOpenPBRTexture(uint textureIndex, uint samplerIndex)
{
    return textureIndex != OPENPBR_INVALID_TEXTURE_INDEX && samplerIndex != OPENPBR_INVALID_TEXTURE_INDEX;
}

MaterialUvSample ResolveOpenPBRTextureUv(
    in MaterialUvCache uvCache,
    uint uvSetIndex,
    bool hasParallaxResolvedUv,
    uint parallaxUvSetIndex,
    float2 parallaxUv,
    float2 parallaxDUdx,
    float2 parallaxDUdy)
{
    if (hasParallaxResolvedUv && uvSetIndex == parallaxUvSetIndex)
    {
        MaterialUvSample sample = (MaterialUvSample)0;
        sample.uvSetIndex = uvSetIndex;
        sample.uv = parallaxUv;
        sample.dUVdx = parallaxDUdx;
        sample.dUVdy = parallaxDUdy;
        return sample;
    }

    uint cacheIndex = FindMaterialUvCacheIndex(uvCache, uvSetIndex);
    if (cacheIndex == MATERIAL_INVALID_UV_CACHE_INDEX)
    {
        cacheIndex = FindMaterialUvCacheIndex(uvCache, 0u);
    }

    if (cacheIndex == MATERIAL_INVALID_UV_CACHE_INDEX || cacheIndex >= uvCache.count)
    {
        return MakeDefaultMaterialUvSample();
    }

    return uvCache.samples[cacheIndex];
}

float4 SampleOpenPBRTexture(uint textureIndex, uint samplerIndex, MaterialUvSample uvSample)
{
    Texture2D<float4> textureHandle = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];
    SamplerState samplerHandle = SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
    return Sample2DGrad(textureHandle, samplerHandle, uvSample.uv, uvSample.dUVdx, uvSample.dUVdy);
}

float3 SampleOpenPBRColorTexture(
    uint textureIndex,
    uint samplerIndex,
    uint4 channels,
    MaterialUvSample uvSample)
{
    if (!HasOpenPBRTexture(textureIndex, samplerIndex))
    {
        return float3(1.0f, 1.0f, 1.0f);
    }

    const float4 sampleValue = SampleOpenPBRTexture(textureIndex, samplerIndex, uvSample);
    return float3(
        DynamicSwizzle(sampleValue, channels.x),
        DynamicSwizzle(sampleValue, channels.y),
        DynamicSwizzle(sampleValue, channels.z));
}

float SampleOpenPBRScalarTexture(
    uint textureIndex,
    uint samplerIndex,
    uint channel,
    MaterialUvSample uvSample)
{
    if (!HasOpenPBRTexture(textureIndex, samplerIndex))
    {
        return 1.0f;
    }

    const float4 sampleValue = SampleOpenPBRTexture(textureIndex, samplerIndex, uvSample);
    return DynamicSwizzle(sampleValue, channel);
}

void ApplyOpenPBRTextureSampling(
    in MaterialUvCache uvCache,
    bool hasParallaxResolvedUv,
    uint parallaxUvSetIndex,
    float2 parallaxUv,
    float2 parallaxDUdx,
    float2 parallaxDUdy,
    in OpenPBRMaterialInfo openPBRMaterialInfo,
    inout OpenPBRSurfaceSample surface)
{
    const MaterialUvSample coatColorUv = ResolveOpenPBRTextureUv(
        uvCache,
        openPBRMaterialInfo.coatColorUvSetIndex,
        hasParallaxResolvedUv,
        parallaxUvSetIndex,
        parallaxUv,
        parallaxDUdx,
        parallaxDUdy);
    const MaterialUvSample coatWeightUv = ResolveOpenPBRTextureUv(
        uvCache,
        openPBRMaterialInfo.coatWeightUvSetIndex,
        hasParallaxResolvedUv,
        parallaxUvSetIndex,
        parallaxUv,
        parallaxDUdx,
        parallaxDUdy);
    const MaterialUvSample coatRoughnessUv = ResolveOpenPBRTextureUv(
        uvCache,
        openPBRMaterialInfo.coatRoughnessUvSetIndex,
        hasParallaxResolvedUv,
        parallaxUvSetIndex,
        parallaxUv,
        parallaxDUdx,
        parallaxDUdy);
    const MaterialUvSample fuzzColorUv = ResolveOpenPBRTextureUv(
        uvCache,
        openPBRMaterialInfo.fuzzColorUvSetIndex,
        hasParallaxResolvedUv,
        parallaxUvSetIndex,
        parallaxUv,
        parallaxDUdx,
        parallaxDUdy);
    const MaterialUvSample fuzzWeightUv = ResolveOpenPBRTextureUv(
        uvCache,
        openPBRMaterialInfo.fuzzWeightUvSetIndex,
        hasParallaxResolvedUv,
        parallaxUvSetIndex,
        parallaxUv,
        parallaxDUdx,
        parallaxDUdy);
    const MaterialUvSample fuzzRoughnessUv = ResolveOpenPBRTextureUv(
        uvCache,
        openPBRMaterialInfo.fuzzRoughnessUvSetIndex,
        hasParallaxResolvedUv,
        parallaxUvSetIndex,
        parallaxUv,
        parallaxDUdx,
        parallaxDUdy);

    surface.coatColor *= SampleOpenPBRColorTexture(
        openPBRMaterialInfo.coatColorTextureIndex,
        openPBRMaterialInfo.coatColorSamplerIndex,
        openPBRMaterialInfo.coatColorChannels,
        coatColorUv);
    surface.coatWeight *= SampleOpenPBRScalarTexture(
        openPBRMaterialInfo.coatWeightTextureIndex,
        openPBRMaterialInfo.coatWeightSamplerIndex,
        openPBRMaterialInfo.coatWeightChannel,
        coatWeightUv);
    surface.coatRoughness *= SampleOpenPBRScalarTexture(
        openPBRMaterialInfo.coatRoughnessTextureIndex,
        openPBRMaterialInfo.coatRoughnessSamplerIndex,
        openPBRMaterialInfo.coatRoughnessChannel,
        coatRoughnessUv);

    surface.fuzzColor *= SampleOpenPBRColorTexture(
        openPBRMaterialInfo.fuzzColorTextureIndex,
        openPBRMaterialInfo.fuzzColorSamplerIndex,
        openPBRMaterialInfo.fuzzColorChannels,
        fuzzColorUv);
    surface.fuzzWeight *= SampleOpenPBRScalarTexture(
        openPBRMaterialInfo.fuzzWeightTextureIndex,
        openPBRMaterialInfo.fuzzWeightSamplerIndex,
        openPBRMaterialInfo.fuzzWeightChannel,
        fuzzWeightUv);
    surface.fuzzRoughness *= SampleOpenPBRScalarTexture(
        openPBRMaterialInfo.fuzzRoughnessTextureIndex,
        openPBRMaterialInfo.fuzzRoughnessSamplerIndex,
        openPBRMaterialInfo.fuzzRoughnessChannel,
        fuzzRoughnessUv);

    surface.coatColor = saturate(surface.coatColor);
    surface.coatWeight = saturate(surface.coatWeight);
    surface.coatRoughness = saturate(surface.coatRoughness);
    surface.fuzzColor = saturate(surface.fuzzColor);
    surface.fuzzWeight = saturate(surface.fuzzWeight);
    surface.fuzzRoughness = saturate(surface.fuzzRoughness);
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
        return (materialFlags & MATERIAL_METALLIC_TEXTURE) != 0u;
    case MATERIAL_TEXTURE_SLOT_ROUGHNESS:
        return (materialFlags & MATERIAL_ROUGHNESS_TEXTURE) != 0u;
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

void AppendOpenPBRForwardUvSamples(
    inout MaterialUvCache cache,
    in VisBufferPSInput input,
    in OpenPBRMaterialInfo openPBRMaterialInfo);

#if defined(CLOD_AVBOIT_FORWARD_TRANSPARENT)
void BuildForwardTransparentMaterialUvData(
    in VisBufferPSInput input,
    in MaterialInfo materialInfo,
    uint materialFlags,
    out MaterialUvCache cache,
    out MaterialUvBindings bindings)
{
    cache = (MaterialUvCache)0;
    bindings = (MaterialUvBindings)0;

    uint cacheIndexByUvSet[MATERIAL_MAX_UNIQUE_UV_SETS];

    [unroll]
    for (uint uvSetIndex = 0u; uvSetIndex < MATERIAL_MAX_UNIQUE_UV_SETS; ++uvSetIndex)
    {
        cacheIndexByUvSet[uvSetIndex] = MATERIAL_INVALID_UV_CACHE_INDEX;
    }

    [unroll]
    for (uint slot = 0u; slot < MATERIAL_TEXTURE_SLOT_COUNT; ++slot)
    {
        bindings.cacheIndexBySlot[slot] = MATERIAL_INVALID_UV_CACHE_INDEX;
    }

    const float4 uvSet01 = input.uvSet01;
    const float4 uvSet23 = input.uvSet23;
    const float4 uvSet45 = input.uvSet45;
    const float4 uvSet67 = input.uvSet67;
    const float4 uvSet01Dx = ddx(uvSet01);
    const float4 uvSet01Dy = ddy(uvSet01);
    const float4 uvSet23Dx = ddx(uvSet23);
    const float4 uvSet23Dy = ddy(uvSet23);
    const float4 uvSet45Dx = ddx(uvSet45);
    const float4 uvSet45Dy = ddy(uvSet45);
    const float4 uvSet67Dx = ddx(uvSet67);
    const float4 uvSet67Dy = ddy(uvSet67);

    [unroll]
    for (uint slot = 0u; slot < MATERIAL_TEXTURE_SLOT_COUNT; ++slot)
    {
        const MaterialTextureSlot textureSlot = (MaterialTextureSlot)slot;
        if (!MaterialSlotEnabled(materialInfo, materialFlags, textureSlot))
        {
            continue;
        }

        const uint uvSetIndex = MaterialSlotUvSetIndex(materialInfo, textureSlot);
        if (uvSetIndex >= MATERIAL_MAX_UNIQUE_UV_SETS || cacheIndexByUvSet[uvSetIndex] != MATERIAL_INVALID_UV_CACHE_INDEX)
        {
            continue;
        }

        const uint cacheIndex = cache.count;
        MaterialUvSample sample = (MaterialUvSample)0;
        sample.uvSetIndex = uvSetIndex;

        switch (uvSetIndex)
        {
        case 0u:
            sample.uv = uvSet01.xy;
            sample.dUVdx = uvSet01Dx.xy;
            sample.dUVdy = uvSet01Dy.xy;
            break;
        case 1u:
            sample.uv = uvSet01.zw;
            sample.dUVdx = uvSet01Dx.zw;
            sample.dUVdy = uvSet01Dy.zw;
            break;
        case 2u:
            sample.uv = uvSet23.xy;
            sample.dUVdx = uvSet23Dx.xy;
            sample.dUVdy = uvSet23Dy.xy;
            break;
        case 3u:
            sample.uv = uvSet23.zw;
            sample.dUVdx = uvSet23Dx.zw;
            sample.dUVdy = uvSet23Dy.zw;
            break;
        case 4u:
            sample.uv = uvSet45.xy;
            sample.dUVdx = uvSet45Dx.xy;
            sample.dUVdy = uvSet45Dy.xy;
            break;
        case 5u:
            sample.uv = uvSet45.zw;
            sample.dUVdx = uvSet45Dx.zw;
            sample.dUVdy = uvSet45Dy.zw;
            break;
        case 6u:
            sample.uv = uvSet67.xy;
            sample.dUVdx = uvSet67Dx.xy;
            sample.dUVdy = uvSet67Dy.xy;
            break;
        default:
            sample.uv = uvSet67.zw;
            sample.dUVdx = uvSet67Dx.zw;
            sample.dUVdy = uvSet67Dy.zw;
            break;
        }

        cache.samples[cacheIndex] = sample;
        cacheIndexByUvSet[uvSetIndex] = cacheIndex;
        cache.count = cacheIndex + 1u;
    }

    AppendOpenPBRForwardUvSamples(cache, input, LoadOpenPBRMaterialInfo(materialInfo));

    [unroll]
    for (uint cacheIndex = 0u; cacheIndex < MATERIAL_MAX_UNIQUE_UV_SETS; ++cacheIndex)
    {
        if (cacheIndex >= cache.count)
        {
            break;
        }

        const uint uvSetIndex = cache.samples[cacheIndex].uvSetIndex;
        if (uvSetIndex < MATERIAL_MAX_UNIQUE_UV_SETS)
        {
            cacheIndexByUvSet[uvSetIndex] = cacheIndex;
        }
    }

    const uint uv0CacheIndex = cacheIndexByUvSet[0u];

    [unroll]
    for (uint slot = 0u; slot < MATERIAL_TEXTURE_SLOT_COUNT; ++slot)
    {
        const MaterialTextureSlot textureSlot = (MaterialTextureSlot)slot;
        if (!MaterialSlotEnabled(materialInfo, materialFlags, textureSlot))
        {
            continue;
        }

        uint cacheIndex = MATERIAL_INVALID_UV_CACHE_INDEX;
        const uint uvSetIndex = MaterialSlotUvSetIndex(materialInfo, textureSlot);
        if (uvSetIndex < MATERIAL_MAX_UNIQUE_UV_SETS)
        {
            cacheIndex = cacheIndexByUvSet[uvSetIndex];
        }
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
}

float2 GetForwardTransparentUvSet(in VisBufferPSInput input, uint uvSetIndex)
{
    switch (uvSetIndex)
    {
    case 0u:
        return input.uvSet01.xy;
    case 1u:
        return input.uvSet01.zw;
    case 2u:
        return input.uvSet23.xy;
    case 3u:
        return input.uvSet23.zw;
    case 4u:
        return input.uvSet45.xy;
    case 5u:
        return input.uvSet45.zw;
    case 6u:
        return input.uvSet67.xy;
    case 7u:
        return input.uvSet67.zw;
    default:
        return float2(0.0f, 0.0f);
    }
}

void AppendForwardMaterialUvSample(inout MaterialUvCache cache, uint uvSetIndex, in VisBufferPSInput input)
{
    if (cache.count >= MATERIAL_MAX_UNIQUE_UV_SETS)
    {
        return;
    }

    const float2 uv = GetForwardTransparentUvSet(input, uvSetIndex);

    MaterialUvSample sample = (MaterialUvSample)0;
    sample.uvSetIndex = uvSetIndex;
    sample.uv = uv;
    sample.dUVdx = ddx(uv);
    sample.dUVdy = ddy(uv);
    cache.samples[cache.count] = sample;
    cache.count++;
}

void AppendOpenPBRForwardUvSamples(
    inout MaterialUvCache cache,
    in VisBufferPSInput input,
    in OpenPBRMaterialInfo openPBRMaterialInfo)
{
    const uint uvSetIndices[6] = {
        openPBRMaterialInfo.coatColorUvSetIndex,
        openPBRMaterialInfo.coatWeightUvSetIndex,
        openPBRMaterialInfo.coatRoughnessUvSetIndex,
        openPBRMaterialInfo.fuzzColorUvSetIndex,
        openPBRMaterialInfo.fuzzWeightUvSetIndex,
        openPBRMaterialInfo.fuzzRoughnessUvSetIndex
    };

    const uint textureIndices[6] = {
        openPBRMaterialInfo.coatColorTextureIndex,
        openPBRMaterialInfo.coatWeightTextureIndex,
        openPBRMaterialInfo.coatRoughnessTextureIndex,
        openPBRMaterialInfo.fuzzColorTextureIndex,
        openPBRMaterialInfo.fuzzWeightTextureIndex,
        openPBRMaterialInfo.fuzzRoughnessTextureIndex
    };

    const uint samplerIndices[6] = {
        openPBRMaterialInfo.coatColorSamplerIndex,
        openPBRMaterialInfo.coatWeightSamplerIndex,
        openPBRMaterialInfo.coatRoughnessSamplerIndex,
        openPBRMaterialInfo.fuzzColorSamplerIndex,
        openPBRMaterialInfo.fuzzWeightSamplerIndex,
        openPBRMaterialInfo.fuzzRoughnessSamplerIndex
    };

    [unroll]
    for (uint i = 0u; i < 6u; ++i)
    {
        if (!HasOpenPBRTexture(textureIndices[i], samplerIndices[i]))
        {
            continue;
        }

        const uint uvSetIndex = uvSetIndices[i];
        if (FindMaterialUvCacheIndex(cache, uvSetIndex) != MATERIAL_INVALID_UV_CACHE_INDEX)
        {
            continue;
        }

        AppendForwardMaterialUvSample(cache, uvSetIndex, input);
    }
}

MaterialUvCache BuildMaterialUvCacheFromForwardInput(
    in VisBufferPSInput input,
    in MaterialInfo materialInfo,
    uint materialFlags)
{
    MaterialUvCache cache = (MaterialUvCache)0;

    [unroll]
    for (uint slot = 0u; slot < MATERIAL_TEXTURE_SLOT_COUNT; ++slot)
    {
        const MaterialTextureSlot textureSlot = (MaterialTextureSlot)slot;
        if (!MaterialSlotEnabled(materialInfo, materialFlags, textureSlot))
        {
            continue;
        }

        const uint uvSetIndex = MaterialSlotUvSetIndex(materialInfo, textureSlot);
        if (FindMaterialUvCacheIndex(cache, uvSetIndex) != MATERIAL_INVALID_UV_CACHE_INDEX)
        {
            continue;
        }

        AppendForwardMaterialUvSample(cache, uvSetIndex, input);
    }

    AppendOpenPBRForwardUvSamples(cache, input, LoadOpenPBRMaterialInfo(materialInfo));

    return cache;
}
#endif

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
            materialInfo.heightMapScale,
            heightUv.dUVdx,
            heightUv.dUVdy);

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

    float metallic = materialInfo.metallicFactor;
    float roughness = materialInfo.roughnessFactor;

#if defined(PSO_METALLIC_TEXTURE)
    Texture2D<float4> metallicTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicTextureIndex)];
    SamplerState metallicSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicSamplerIndex)];

    float2 metallicSampleUv = metallicUv.uv;
    float2 metallicDUdx = metallicUv.dUVdx;
    float2 metallicDUdy = metallicUv.dUVdy;
    if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_METALLIC] == uvBindings.heightCacheIndex)
    {
        metallicSampleUv = parallaxUv;
        metallicDUdx = parallaxDUdx;
        metallicDUdy = parallaxDUdy;
    }

    float4 metallicSample = Sample2DGrad(metallicTexture, metallicSamplerState, metallicSampleUv, metallicDUdx, metallicDUdy);
    metallic = DynamicSwizzle(metallicSample, materialInfo.metallicChannel) * materialInfo.metallicFactor;
#endif

#if defined(PSO_ROUGHNESS_TEXTURE)
    Texture2D<float4> roughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessTextureIndex)];
    SamplerState roughnessSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessSamplerIndex)];

    float2 roughnessSampleUv = roughnessUv.uv;
    float2 roughnessDUdx = roughnessUv.dUVdx;
    float2 roughnessDUdy = roughnessUv.dUVdy;
    if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_ROUGHNESS] == uvBindings.heightCacheIndex)
    {
        roughnessSampleUv = parallaxUv;
        roughnessDUdx = parallaxDUdx;
        roughnessDUdy = parallaxDUdy;
    }

    float4 roughnessSample = Sample2DGrad(roughnessTexture, roughnessSamplerState, roughnessSampleUv, roughnessDUdx, roughnessDUdy);
    roughness = DynamicSwizzle(roughnessSample, materialInfo.roughnessChannel) * materialInfo.roughnessFactor;
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
    float4 aoSample = Sample2DGrad(aoTexture, aoSamplerState, aoSampleUv, aoDUdx, aoDUdy);
    ao = DynamicSwizzle(aoSample, materialInfo.aoChannel);
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

    OpenPBRSurfaceSample openPBRSurface = ResolveCanonicalOpenPBRSurface(
        materialInfo,
        baseColor.rgb * vertexColorMultiplier,
        metallic,
        roughness,
        baseColor.a,
        emissive);
    ApplyOpenPBRTextureSampling(
        uvCache,
        hasParallaxResolvedUv,
        materialInfo.heightUvSetIndex,
        parallaxUv,
        parallaxDUdx,
        parallaxDUdy,
        LoadOpenPBRMaterialInfo(materialInfo),
        openPBRSurface);
    PopulateLegacyMaterialInputsFromOpenPBRSurface(openPBRSurface, normalWS, ao, ret);
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
            materialInfo.heightMapScale,
            heightUv.dUVdx,
            heightUv.dUVdy);

        parallaxUv = uvh.xy;
        parallaxDUdx = heightUv.dUVdx;
        parallaxDUdy = heightUv.dUVdy;
        hasParallaxResolvedUv = true;
    }

    float4 baseColor = materialInfo.baseColorFactor;

    if ((materialFlags & MATERIAL_BASE_COLOR_TEXTURE) != 0u)
    {
        const MaterialUvSample baseColorUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_BASE_COLOR);
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
        const MaterialUvSample opacityUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_OPACITY);
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
    if ((materialFlags & MATERIAL_METALLIC_TEXTURE) != 0u)
    {
        const MaterialUvSample metallicUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_METALLIC);
        Texture2D<float4> metallicTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicTextureIndex)];
        SamplerState metallicSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicSamplerIndex)];

        float2 metallicSampleUv = metallicUv.uv;
        float2 metallicDUdx = metallicUv.dUVdx;
        float2 metallicDUdy = metallicUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_METALLIC] == uvBindings.heightCacheIndex)
        {
            metallicSampleUv = parallaxUv;
            metallicDUdx = parallaxDUdx;
            metallicDUdy = parallaxDUdy;
        }

        float4 metallicSample = Sample2DGrad(metallicTexture, metallicSamplerState, metallicSampleUv, metallicDUdx, metallicDUdy);
        metallic = DynamicSwizzle(metallicSample, materialInfo.metallicChannel) * materialInfo.metallicFactor;
    }

    if ((materialFlags & MATERIAL_ROUGHNESS_TEXTURE) != 0u)
    {
        const MaterialUvSample roughnessUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_ROUGHNESS);
        Texture2D<float4> roughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessTextureIndex)];
        SamplerState roughnessSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessSamplerIndex)];

        float2 roughnessSampleUv = roughnessUv.uv;
        float2 roughnessDUdx = roughnessUv.dUVdx;
        float2 roughnessDUdy = roughnessUv.dUVdy;
        if (hasParallaxResolvedUv && uvBindings.cacheIndexBySlot[MATERIAL_TEXTURE_SLOT_ROUGHNESS] == uvBindings.heightCacheIndex)
        {
            roughnessSampleUv = parallaxUv;
            roughnessDUdx = parallaxDUdx;
            roughnessDUdy = parallaxDUdy;
        }

        float4 roughnessSample = Sample2DGrad(roughnessTexture, roughnessSamplerState, roughnessSampleUv, roughnessDUdx, roughnessDUdy);
        roughness = DynamicSwizzle(roughnessSample, materialInfo.roughnessChannel) * materialInfo.roughnessFactor;
    }

    float3 normalWS = normalWSBase;
    if ((materialFlags & MATERIAL_NORMAL_MAP) != 0u && uvBindings.hasTbnSource)
    {
        const MaterialUvSample normalUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_NORMAL);
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
        const MaterialUvSample aoUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_AO);
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
        float4 aoSample = Sample2DGrad(aoTexture, aoSamplerState, aoSampleUv, aoDUdx, aoDUdy);
        ao = DynamicSwizzle(aoSample, materialInfo.aoChannel);
    }

    float3 emissive = materialInfo.emissiveFactor.rgb;
    if ((materialFlags & MATERIAL_EMISSIVE_TEXTURE) != 0u)
    {
        const MaterialUvSample emissiveUv = GetBoundUvSample(uvCache, uvBindings, MATERIAL_TEXTURE_SLOT_EMISSIVE);
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

    OpenPBRSurfaceSample openPBRSurface = ResolveCanonicalOpenPBRSurface(
        materialInfo,
        baseColor.rgb * vertexColorMultiplier,
        metallic,
        roughness,
        baseColor.a,
        emissive);
    PopulateLegacyMaterialInputsFromOpenPBRSurface(openPBRSurface, normalWS, ao, ret);
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
    float metallic = materialInfo.metallicFactor;
    float roughness = materialInfo.roughnessFactor;

#if defined(PSO_METALLIC_TEXTURE)
    Texture2D<float4> metallicTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicTextureIndex)];
    SamplerState metallicSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.metallicSamplerIndex)];
    float4 metallicSample  = Sample2DGrad(metallicTexture,  metallicSamplerState,  localUV, localDUdx, localDUdy);
    metallic  = DynamicSwizzle(metallicSample,  materialInfo.metallicChannel)  * materialInfo.metallicFactor;
#endif

#if defined(PSO_ROUGHNESS_TEXTURE)
    Texture2D<float4> roughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessTextureIndex)];
    SamplerState roughnessSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.roughnessSamplerIndex)];
    float4 roughnessSample = Sample2DGrad(roughnessTexture, roughnessSamplerState, localUV, localDUdx, localDUdy);
    roughness = DynamicSwizzle(roughnessSample, materialInfo.roughnessChannel) * materialInfo.roughnessFactor;
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
    float4 aoSample = Sample2DGrad(aoTexture, aoSamplerState, localUV, localDUdx, localDUdy);
    ao = DynamicSwizzle(aoSample, materialInfo.aoChannel);
#endif

    // Emissive
    float3 emissive = materialInfo.emissiveFactor.rgb;
#if defined(PSO_EMISSIVE_TEXTURE)
    Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.emissiveTextureIndex)];
    SamplerState emissiveSamplerState = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.emissiveSamplerIndex)];
    emissive = Sample2DGrad(emissiveTexture, emissiveSamplerState, localUV, localDUdx, localDUdy).rgb * materialInfo.emissiveFactor.rgb;
#endif

    OpenPBRSurfaceSample openPBRSurface = ResolveCanonicalOpenPBRSurface(
        materialInfo,
        baseColor.rgb,
        metallic,
        roughness,
        baseColor.a,
        emissive);
    ApplyOpenPBRTextureSampling(
        BuildSingleUvCache(localUV, localDUdx, localDUdy),
        false,
        0u,
        float2(0.0f, 0.0f),
        float2(0.0f, 0.0f),
        float2(0.0f, 0.0f),
        LoadOpenPBRMaterialInfo(materialInfo),
        openPBRSurface);
    PopulateLegacyMaterialInputsFromOpenPBRSurface(openPBRSurface, normalWS, ao, ret);
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

float OpenPBRIorToF0(float ior)
{
    const float safeIor = max(ior, 1.0f);
    const float f = (safeIor - 1.0f) / (safeIor + 1.0f);
    return f * f;
}

float OpenPBRIorFromF0(float f0)
{
    const float safeF0 = min(saturate(f0), 0.9999f);
    const float sqrtF0 = sqrt(safeF0);
    return (1.0f + sqrtF0) / max(1.0f - sqrtF0, 1.0e-4f);
}

float OpenPBRApplySpecularWeightToIor(float ior, float specularWeight)
{
    const float unscaledF0 = OpenPBRIorToF0(ior);
    const float scaledF0 = min(unscaledF0 * saturate(specularWeight), 0.9999f);
    return OpenPBRIorFromF0(scaledF0);
}

float3 OpenPBRComputeMetalSchlickBFactor(float3 f0, float3 f82Tint)
{
    const float cosThetaMax = 1.0f / 7.0f;
    const float oneMinusCosThetaMax = 1.0f - cosThetaMax;
    const float oneMinusCosThetaMaxToTheFifth = pow(oneMinusCosThetaMax, 5.0f);
    const float oneMinusCosThetaMaxToTheSixth = pow(oneMinusCosThetaMax, 6.0f);
    const float3 whiteMinusF0 = 1.0f.xxx - saturate(f0);
    const float3 whiteMinusTint = 1.0f.xxx - saturate(f82Tint);
    const float3 numerator = (saturate(f0) + whiteMinusF0 * oneMinusCosThetaMaxToTheFifth) * whiteMinusTint;
    const float denominator = cosThetaMax * oneMinusCosThetaMaxToTheSixth;
    return numerator / max(denominator, 1.0e-6f);
}

float3 OpenPBRMetalAverageFresnelWithF82Tint(float3 f0, float3 f82Tint)
{
    const float3 safeF0 = saturate(f0);
    const float3 whiteMinusF0 = 1.0f.xxx - safeF0;
    const float3 b = OpenPBRComputeMetalSchlickBFactor(safeF0, f82Tint);
    return saturate(safeF0 + whiteMinusF0 * (1.0f / 21.0f) - b * (1.0f / 126.0f));
}

void PopulateFragmentInfoFromOpenPBR(
    OpenPBRSurfaceSample surface,
    inout FragmentInfo ret)
{
    OpenPBRMaterialInfo openPBRMaterialInfo = LoadOpenPBRMaterialInfo(surface.openPBRMaterialDataIndex);
    const float baseWeight = saturate(openPBRMaterialInfo.baseWeight);
    const float specularWeight = saturate(openPBRMaterialInfo.specularWeight);
    const float3 specularColor = saturate(openPBRMaterialInfo.specularColor);
    const float3 weightedBaseColor = saturate(surface.baseColor * baseWeight);
    const float weightedSpecularIor = OpenPBRApplySpecularWeightToIor(openPBRMaterialInfo.specularIor, specularWeight);
    const float dielectricF0Scalar = OpenPBRIorToF0(weightedSpecularIor);
    const float3 dielectricF0 = saturate(specularColor * dielectricF0Scalar);
    const float dielectricF0Max = max(max(dielectricF0.x, dielectricF0.y), dielectricF0.z);
    const float coatPerceptualRoughness = clamp(surface.coatRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0f);
    const float coatF0Scalar = OpenPBRIorToF0(openPBRMaterialInfo.coatIor);
    const float dielectricSpecularWeight = saturate(1.0f - surface.baseMetalness);
    const float metalSpecularWeight = saturate(surface.baseMetalness * specularWeight);
    const float3 metalF0 = saturate(weightedBaseColor * specularColor);
    const float3 metalAverageFresnel = OpenPBRMetalAverageFresnelWithF82Tint(weightedBaseColor, specularColor);
    const float specularAlpha = ret.roughness;

    ret.openPBRMaterialDataIndex = surface.openPBRMaterialDataIndex;
    ret.albedo = weightedBaseColor;
    ret.emissive = surface.emissive;
    ret.metallic = surface.baseMetalness;
    ret.coatWeight = saturate(surface.coatWeight);
    ret.coatColor = saturate(surface.coatColor);
    ret.coatPerceptualRoughness = coatPerceptualRoughness;
    ret.coatRoughness = PerceptualRoughnessToRoughness(coatPerceptualRoughness);
    ret.coatF0 = saturate(ret.coatColor * coatF0Scalar);
    ret.coatIor = openPBRMaterialInfo.coatIor;
    ret.coatDarkening = saturate(openPBRMaterialInfo.coatDarkening);
    ret.fuzzWeight = saturate(surface.fuzzWeight);
    ret.fuzzColor = saturate(surface.fuzzColor);
    ret.fuzzRoughness = saturate(surface.fuzzRoughness);
    ret.baseDiffuseRoughness = saturate(openPBRMaterialInfo.baseDiffuseRoughness);
    ret.specularAlpha = specularAlpha;
    ret.weightedSpecularIor = weightedSpecularIor;
    ret.dielectricSpecularWeight = dielectricSpecularWeight;
    ret.dielectricSpecularF0 = dielectricF0;
    ret.metalSpecularWeight = metalSpecularWeight;
    ret.metalSpecularF0 = metalF0;
    ret.metalAverageFresnel = metalAverageFresnel;
    ret.diffuseColor = computeDiffuseColor(weightedBaseColor, surface.baseMetalness);
    ret.reflectance = sqrt(saturate(dielectricF0Max / 0.16f));
    ret.dielectricF0 = dielectricF0Max * (1.0f - surface.baseMetalness);
    ret.F0 = saturate(dielectricSpecularWeight * dielectricF0 + metalSpecularWeight * metalF0);
}

void GetFragmentInfoScreenSpace(in uint2 pixelCoordinates, in float3 viewWS, in float3 fragPosViewSpace, in float3 fragPosWorldSpace, in bool enableGTAO, out FragmentInfo ret) {
    ret.pixelCoords = pixelCoordinates;
    ret.fragPosViewSpace = fragPosViewSpace;
    ret.fragPosWorldSpace = fragPosWorldSpace;
    
    // Gather textures
    Texture2D<float4> normalsTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Normals)];
    
    // Load values
    float4 normalSample = normalsTexture[pixelCoordinates];
    ret.normalWS = normalSample.xyz;
    //ret.normalWS = SignedOctDecode(encodedNormal.yzw);
    
    Texture2D<float4> albedoTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Albedo)];
    float4 baseColorSample = albedoTexture[pixelCoordinates];
    Texture2D<float4> coatTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Coat)];
    float4 coatSample = coatTexture[pixelCoordinates];
    Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Emissive)];
    float4 emissive = emissiveTexture[pixelCoordinates];
    Texture2D<float4> fuzzTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Fuzz)];
    float4 fuzzSample = fuzzTexture[pixelCoordinates];
    
    if (enableGTAO)
    {
        Texture2D<uint> aoTexture = ResourceDescriptorHeap[OptionalResourceDescriptorIndex(Builtin::GTAO::OutputAOTerm)];
        ret.diffuseAmbientOcclusion = min(baseColorSample.w, float(aoTexture[pixelCoordinates].x) / 255.0);
    }
    else
    {
        ret.diffuseAmbientOcclusion = baseColorSample.w; // AO stored in alpha channel
    }
    
    Texture2D<float4> metallicRoughnessTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::MetallicRoughness)];
    float4 metallicRoughness = metallicRoughnessTexture[pixelCoordinates];
    
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

    ret.alpha = 1.0; // Opaque objects

    OpenPBRSurfaceSample surface = (OpenPBRSurfaceSample)0;
    surface.openPBRMaterialDataIndex = (uint)(normalSample.w + 0.5f);
    surface.baseColor = baseColorSample.xyz;
    surface.baseMetalness = metallicRoughness.x;
    surface.specularRoughness = metallicRoughness.y;
    surface.coatColor = coatSample.xyz;
    surface.coatWeight = coatSample.w;
    surface.coatRoughness = metallicRoughness.z;
    surface.fuzzColor = fuzzSample.xyz;
    surface.fuzzWeight = metallicRoughness.w;
    surface.fuzzRoughness = fuzzSample.w;
    surface.opacity = 1.0f;
    surface.emissive = emissive.xyz;
    PopulateFragmentInfoFromOpenPBR(surface, ret);
}

void FillFragmentInfoDirect(inout FragmentInfo ret, in MaterialInputs materialInfo, in float3 viewWS, in float2 pixelCoords, in bool transparent, in bool isFrontFace, in uint materialFlags)
{
    ret.materialFlags = materialFlags;
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

    OpenPBRSurfaceSample surface = (OpenPBRSurfaceSample)0;
    surface.openPBRMaterialDataIndex = materialInfo.openPBRMaterialDataIndex;
    surface.baseColor = materialInfo.albedo;
    surface.baseMetalness = materialInfo.metallic;
    surface.specularRoughness = materialInfo.roughness;
    surface.coatColor = materialInfo.coatColor;
    surface.coatWeight = materialInfo.coatWeight;
    surface.coatRoughness = materialInfo.coatRoughness;
    surface.fuzzColor = materialInfo.fuzzColor;
    surface.fuzzWeight = materialInfo.fuzzWeight;
    surface.fuzzRoughness = materialInfo.fuzzRoughness;
    surface.opacity = materialInfo.opacity;
    surface.emissive = materialInfo.emissive;
    PopulateFragmentInfoFromOpenPBR(surface, ret);

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
