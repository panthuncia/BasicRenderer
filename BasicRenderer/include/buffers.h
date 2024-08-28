#pragma once
#include <DirectXMath.h>

struct PerFrameCB {
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX projection;
    DirectX::XMVECTOR eyePosWorldSpace;
};

struct PerObjectCB {
    DirectX::XMMATRIX model;
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
    DirectX::XMFLOAT4 baseColorFactor;
    DirectX::XMFLOAT4 emissiveFactor;
};

struct LightInfo {
    DirectX::XMVECTOR properties;
    DirectX::XMVECTOR posWorldSpace;
    DirectX::XMVECTOR dirWorldSpace;
    DirectX::XMVECTOR attenuation;
    DirectX::XMVECTOR color;
};