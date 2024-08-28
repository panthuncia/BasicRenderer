#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <unordered_map>
#include <string>

#include "DirectX/dxcapi.h"

using Microsoft::WRL::ComPtr;

enum PSOFlags {
    VERTEX_COLORS = 1 << 0,
    BASE_COLOR_TEXTURE = 1 << 1,
    NORMAL_MAP = 1 << 2,
    AO_TEXTURE = 1<<3,
    EMISSIVE_TEXTURE = 1<<4,
    PBR = 1 << 5,
    PBR_MAPS = 1 << 6,
    SKINNED = 1 << 7,
};

class PSOManager {
public:
    static PSOManager& getInstance();

    void initialize();

    ComPtr<ID3D12PipelineState> GetPSO(UINT psoFlags);
    ComPtr<ID3D12RootSignature> GetRootSignature();

private:
    PSOManager() = default;
    ComPtr<ID3D12RootSignature> rootSignature;
    std::unordered_map<UINT, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_psoCache;
    ComPtr<IDxcUtils> pUtils;
    ComPtr<IDxcCompiler3> pCompiler;

    ComPtr<ID3D12PipelineState> CreatePSO(UINT psoFlags);
    std::vector<DxcDefine> GetShaderDefines(UINT psoFlags);
    void CompileShader(const std::wstring& filename, const std::wstring& entryPoint, const std::wstring& target, std::vector<DxcDefine> defines, Microsoft::WRL::ComPtr<ID3DBlob>& shaderBlob);
    void createRootSignature();
};

inline PSOManager& PSOManager::getInstance() {
    static PSOManager instance;
    return instance;
}