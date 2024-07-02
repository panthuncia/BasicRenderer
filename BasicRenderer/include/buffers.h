#pragma once
#include <DirectXMath.h>

struct PerFrameCB {
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX projection;
    DirectX::XMVECTOR eyePosWorldSpace;
};

struct PerMeshCB {
    DirectX::XMMATRIX model;
};

struct PerMaterialCB {
    DirectX::XMFLOAT4 color;
};

struct LightInfo {
    DirectX::XMVECTOR properties;
    DirectX::XMVECTOR posWorldSpace;
    DirectX::XMVECTOR dirWorldSpace;
    DirectX::XMVECTOR attenuation;
    DirectX::XMVECTOR color;
};