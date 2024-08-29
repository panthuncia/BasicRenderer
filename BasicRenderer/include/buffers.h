#pragma once
#include <DirectXMath.h>

struct PerFrameCB {
    DirectX::XMMATRIX viewMatrix;
    DirectX::XMMATRIX projectionMatrix;
    DirectX::XMVECTOR eyePosWorldSpace;
    DirectX::XMVECTOR ambientLighting;
};

struct PerObjectCB {
    DirectX::XMMATRIX modelMatrix;
    DirectX::XMMATRIX normalMatrix;
};

struct PerMeshCB {
    UINT materialDataIndex;
};

struct PerMaterialCB {
    UINT psoFlags;
    UINT baseColorTextureIndex;
    UINT baseColorSamplerIndex;
    UINT normalTextureIndex;
    UINT normalSamplerIndex;
    UINT metallicRoughnessTextureIndex;
    UINT metallicRoughnessSamplerIndex;
    UINT emissiveTextureIndex;
    UINT emissiveSamplerIndex;
    UINT aoMapIndex;
    UINT aoSamplerIndex;
    UINT heightMapIndex;
    UINT heightSamplerIndex;
    float metallicFactor;
    float roughnessFactor;
    float ambientStrength;
    float specularStrength;
    float textureScale;
    float heightMapScale;
    float pad0;
    DirectX::XMFLOAT4 baseColorFactor;
    DirectX::XMFLOAT4 emissiveFactor;
};

struct LightInfo {
    // Light attributes: x=type (0=point, 1=spot, 2=directional)
    // x=point -> w = shadow caster
    // x=spot -> y= inner cone angle, z= outer cone angle, w= shadow caster
    // x=directional => w= shadow caster
    DirectX::XMVECTOR properties;
    DirectX::XMVECTOR posWorldSpace; // Position of the lights
    DirectX::XMVECTOR dirWorldSpace; // Direction of the lights
    DirectX::XMVECTOR attenuation; // x,y,z = constant, linear, quadratic attenuation, w= max range
    DirectX::XMVECTOR color; // Color of the lights
};