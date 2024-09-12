#pragma once

#include <windows.h>
#include <iostream>
#include <wrl.h>
#include <d3dcompiler.h>

#include "RenderableObject.h"
#include "GlTFLoader.h"

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