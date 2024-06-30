//
// Created by matth on 6/25/2024.
//
#include <windows.h>
#include <iostream>
#include <wrl.h>
#include <d3dcompiler.h>

#include "RenderableObject.h"
#include "GlTFLoader.h"

#ifndef UTILITIES_H
#define UTILITIES_H

void ThrowIfFailed(HRESULT hr);

template<typename... Args>
void print(Args... args) {
    (std::cout << ... << args) << std::endl;
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& target);
#endif //UTILITIES_H

std::shared_ptr<RenderableObject> RenderableFromData(MeshData meshData, std::string name);