#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <unordered_map>
#include <string>

using Microsoft::WRL::ComPtr;

enum PSOFlags {
    VERTEX_COLORS = 1 << 0,
    NORMAL_MAP = 1 << 1,
    AO_TEXTURE = 1<<2,
    EMISSIVE_TEXTURE = 1<<3,
    PBR = 1 << 4,
    SKINNED = 1 << 5,
};

class PSOManager {
public:
    static PSOManager& getInstance();

    void initialize();

    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPSO(UINT psoFlags);

private:
    PSOManager() = default;
    ComPtr<ID3D12RootSignature> rootSignature;
    std::unordered_map<UINT, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_psoCache;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePSO(UINT psoFlags);
    std::vector<D3D_SHADER_MACRO> GetShaderDefines(UINT psoFlags);
    void CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& target, const D3D_SHADER_MACRO* defines, Microsoft::WRL::ComPtr<ID3DBlob>& shaderBlob);
    void createRootSignature();
};

inline PSOManager& PSOManager::getInstance() {
    static PSOManager instance;
    return instance;
}

inline void PSOManager::initialize() {
    createRootSignature();
}