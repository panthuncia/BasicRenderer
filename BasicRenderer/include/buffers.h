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
    UINT normalTextureIndex;
    UINT metallicRoughnessTextureIndex;
    UINT emissiveTextureIndex;
    UINT aoMapIndex;
    UINT heightMapIndex;
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