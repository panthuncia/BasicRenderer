#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <unordered_map>
#include <string>

#include "ThirdParty/DirectX/dxcapi.h"
#include "Materials/BlendState.h"
#include "Render/PSOFlags.h"

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
    static PSOManager& GetInstance();

    void initialize();

    ComPtr<ID3D12PipelineState> GetPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
	ComPtr<ID3D12PipelineState> GetPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    ComPtr<ID3D12PipelineState> GetMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
	ComPtr<ID3D12PipelineState> GetMeshPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    ComPtr<ID3D12PipelineState> GetPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    ComPtr<ID3D12PipelineState> GetMeshPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

	ComPtr<ID3D12PipelineState> GetShadowPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
	ComPtr<ID3D12PipelineState> GetShadowMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

	ComPtr<ID3D12PipelineState> GetDeferredPSO(UINT psoFlags);

    ComPtr<ID3D12RootSignature> GetRootSignature();
	ComPtr<ID3D12RootSignature> GetComputeRootSignature();
    void ReloadShaders();
    void CompileShader(const std::wstring& filename, const std::wstring& entryPoint, const std::wstring& target, std::vector<DxcDefine> defines, Microsoft::WRL::ComPtr<ID3DBlob>& shaderBlob);
    std::vector<DxcDefine> GetShaderDefines(UINT psoFlags);

private:
    PSOManager() = default;
    ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12RootSignature> computeRootSignature;
    ComPtr<ID3D12RootSignature> debugRootSignature;
    ComPtr<ID3D12RootSignature> environmentConversionRootSignature;

    std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_psoCache;
    std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PPLLPSOCache;
    std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_meshPSOCache;
    std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_meshPPLLPSOCache;

    std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_prePassPSOCache;
    std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_meshPrePassPSOCache;

    std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_shadowPSOCache;
	std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_shadowMeshPSOCache;

	std::unordered_map<unsigned int, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_deferredPSOCache;

    ComPtr<IDxcUtils> pUtils;
    ComPtr<IDxcCompiler3> pCompiler;
	ComPtr<ID3D12PipelineState> debugPSO;
    ComPtr<ID3D12PipelineState> environmentConversionPSO;

    ComPtr<ID3D12PipelineState> CreatePSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    ComPtr<ID3D12PipelineState> CreatePPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    ComPtr<ID3D12PipelineState> CreateMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    ComPtr<ID3D12PipelineState> CreateMeshPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    ComPtr<ID3D12PipelineState> CreatePrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    ComPtr<ID3D12PipelineState> CreateMeshPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    ComPtr<ID3D12PipelineState> CreateShadowPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
	ComPtr<ID3D12PipelineState> CreateShadowMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

	ComPtr<ID3D12PipelineState> CreateDeferredPSO(UINT psoFlags);

    void createRootSignature();
    D3D12_BLEND_DESC GetBlendDesc(BlendState blendState);
};

inline PSOManager& PSOManager::GetInstance() {
    static PSOManager instance;
    return instance;
}