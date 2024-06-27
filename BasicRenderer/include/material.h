#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <string>
#include "buffers.h"

using Microsoft::WRL::ComPtr;

class Material {
public:
    Material(ComPtr<ID3D12Device> device, const std::wstring& shaderFile);
    void Bind(ComPtr<ID3D12GraphicsCommandList> commandList);
    void UpdateConstantBuffer(const PerMaterialCB& cbData);

private:
    void LoadShaders(const std::wstring& shaderFile);
    void CreatePipelineState();
    void CreateRootSignature();
    void CreateConstantBuffer();

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12PipelineState> pipelineState;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3D12Resource> constantBuffer;
    UINT8* pConstantBuffer;

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[2];
};
