#pragma once
#include <DirectXMath.h>

struct PerFrameCB {
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX projection;
};

struct PerMeshCB {
    DirectX::XMMATRIX model;
};

struct PerMaterialCB {
    DirectX::XMFLOAT4 color;
};