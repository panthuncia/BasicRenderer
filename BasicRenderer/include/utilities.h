#pragma once

#include <windows.h>
#include <iostream>
#include <wrl.h>
#include <d3dcompiler.h>

#include "RenderableObject.h"
#include "GlTFLoader.h"
#include "DefaultDirection.h"
#include "Light.h"

void ThrowIfFailed(HRESULT hr);

template<typename... Args>
void print(Args... args) {
    (std::cout << ... << args) << std::endl;
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& target);

std::shared_ptr<RenderableObject> RenderableFromData(MeshData meshData, std::string name);

XMMATRIX RemoveScalingFromMatrix(XMMATRIX& initialMatrix);
std::shared_ptr<Texture> loadTextureFromFile(const char* filename);

template <typename T1, typename T2>
bool mapHasKeyNotAsValue(std::unordered_map<T1, T2>& map, T1 key, T2 val) {
    return map.contains(key) && map[key] != val;
}

struct Cascade {
    float size;
    XMVECTOR center;
    XMMATRIX orthoMatrix;
    XMMATRIX viewMatrix;
};

DirectX::XMMATRIX createDirectionalLightViewMatrix(XMVECTOR lightDir, XMVECTOR center) {
    auto mat = XMMatrixLookAtRH(center, XMVectorAdd(center, lightDir), defaultDirection);
    return mat;
}

std::vector<Cascade> setupCascades(int numCascades, Light& light, Camera& camera, const std::vector<float>& cascadeSplits) {
    std::vector<Cascade> cascades;

    for (int i = 0; i < numCascades; ++i) {
        float size = cascadeSplits[i];
        auto pos = camera.transform.getGlobalPosition();
        XMVECTOR camPos = XMLoadFloat3(&pos);
        XMVECTOR center = XMVectorSet(XMVectorGetX(camPos), 0.0f, XMVectorGetZ(camPos), 0.0f);
        XMMATRIX viewMatrix = createDirectionalLightViewMatrix(light.GetLightDir(), center);
        XMMATRIX orthoMatrix = XMMatrixOrthographicRH(size, size, -20.0f, 100.0f);

        cascades.push_back({ size, center, orthoMatrix, viewMatrix });
    }

    return cascades;
}