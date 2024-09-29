#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <unordered_map>
#include <string>

#include "DirectX/dxcapi.h"
#include "BlendState.h"
#include "PSOFlags.h"

using Microsoft::WRL::ComPtr;

struct PSOKey {
    UINT psoFlags;
    BlendState blendState;
	bool wireframe;

    PSOKey(UINT flags, BlendState blend, bool wireframe) : psoFlags(flags), blendState(blend), wireframe(wireframe) {}

    bool operator==(const PSOKey& other) const {
        return psoFlags == other.psoFlags && blendState == other.blendState && wireframe == other.wireframe;
    }
};

namespace std {
    template <>
    struct hash<PSOKey> {
        std::size_t operator()(const PSOKey& key) const noexcept {
            // Combine the hash of psoFlags, blendState, and wireframe
            std::size_t h1 = std::hash<UINT>{}(key.psoFlags);
            std::size_t h2 = std::hash<int>{}(static_cast<int>(key.blendState)); // Cast to int for hashing
			std::size_t h3 = std::hash<bool>{}(key.wireframe);

            // Boost's hash_combine equivalent
            std::size_t combinedHash = h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            combinedHash ^= (h3 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2));

            return combinedHash;
        }
    };
}

class PSOManager {
public:
    static PSOManager& getInstance();

    void initialize();

    ComPtr<ID3D12PipelineState> GetPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    ComPtr<ID3D12PipelineState> GetDebugPSO();

    ComPtr<ID3D12RootSignature> GetRootSignature();
	ComPtr<ID3D12PipelineState> GetSkyboxPSO();
	ComPtr<ID3D12PipelineState> GetEnvironmentConversionPSO();
	ComPtr<ID3D12RootSignature> GetDebugRootSignature();
    ComPtr<ID3D12RootSignature> GetSkyboxRootSignature();
    ComPtr<ID3D12RootSignature> GetEnvironmentConversionRootSignature();
    void ReloadShaders();

private:
    PSOManager() = default;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12RootSignature> debugRootSignature;
    ComPtr<ID3D12RootSignature> skyboxRootSignature;
    ComPtr<ID3D12RootSignature> environmentConversionRootSignature;
    std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_psoCache;
    ComPtr<IDxcUtils> pUtils;
    ComPtr<IDxcCompiler3> pCompiler;
	ComPtr<ID3D12PipelineState> debugPSO;
    ComPtr<ID3D12PipelineState> skyboxPSO;
    ComPtr<ID3D12PipelineState> environmentConversionPSO;

    ComPtr<ID3D12PipelineState> CreatePSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    std::vector<DxcDefine> GetShaderDefines(UINT psoFlags);
    void CompileShader(const std::wstring& filename, const std::wstring& entryPoint, const std::wstring& target, std::vector<DxcDefine> defines, Microsoft::WRL::ComPtr<ID3DBlob>& shaderBlob);
    void createRootSignature();
    D3D12_BLEND_DESC GetBlendDesc(BlendState blendState);
    void CreateDebugRootSignature();
    void CreateDebugPSO();
    void CreateSkyboxRootSignature();
    void CreateSkyboxPSO();
	void CreateEnvironmentConversionRootSignature();
	void CreateEnvironmentConversionPSO();
};

inline PSOManager& PSOManager::getInstance() {
    static PSOManager instance;
    return instance;
}