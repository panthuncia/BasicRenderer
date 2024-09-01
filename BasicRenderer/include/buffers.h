#pragma once
#include <DirectXMath.h>

struct PerFrameCB {
    DirectX::XMMATRIX viewMatrix;
    DirectX::XMMATRIX projectionMatrix;
    DirectX::XMVECTOR eyePosWorldSpace;
    DirectX::XMVECTOR ambientLighting;
    unsigned int lightBufferIndex;
    unsigned int numLights;
};

struct PerObjectCB {
    DirectX::XMMATRIX modelMatrix;
    DirectX::XMMATRIX normalMatrix;
    unsigned int boneTransformBufferIndex;
    unsigned int inverseBindMatricesBufferIndex;
};

struct PerMeshCB {
    unsigned int materialDataIndex;
};

struct PerMaterialCB {
    unsigned int psoFlags;
    unsigned int baseColorTextureIndex;
    unsigned int baseColorSamplerIndex;
    unsigned int normalTextureIndex;
    unsigned int normalSamplerIndex;
    unsigned int metallicRoughnessTextureIndex;
    unsigned int metallicRoughnessSamplerIndex;
    unsigned int emissiveTextureIndex;
    unsigned int emissiveSamplerIndex;
    unsigned int aoMapIndex;
    unsigned int aoSamplerIndex;
    unsigned int heightMapIndex;
    unsigned int heightSamplerIndex;
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
    unsigned int type;
    float innerConeAngle;
    float outerConeAngle;
    unsigned int shadowCaster;
    DirectX::XMVECTOR posWorldSpace; // Position of the lights
    DirectX::XMVECTOR dirWorldSpace; // Direction of the lights
    DirectX::XMVECTOR attenuation; // x,y,z = constant, linear, quadratic attenuation, w= max range
    DirectX::XMVECTOR color; // Color of the lights
};