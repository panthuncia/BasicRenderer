#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <unordered_map>
#include <string>
#include <filesystem>
#include <optional>

#pragma warning(push, 0)   // Disable all warnings for dxc header
#include "ThirdParty/DirectX/dxcapi.h"
#pragma warning(pop)
#include "Materials/BlendState.h"
#include "Render/PSOFlags.h"
#include "Render/PipelineState.h"
#include "Resources/ResourceIdentifier.h"

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

struct ShaderInfo {
    std::wstring filename;
    std::wstring entryPoint;
    std::wstring target;
    ShaderInfo(const std::wstring& file, const std::wstring& entry, const std::wstring& tgt)
        : filename(file), entryPoint(entry), target(tgt) {}
};
struct ShaderInfoBundle {
    ShaderInfoBundle(const std::vector<DxcDefine>& defs, bool debug = false, bool warnings = true) : 
        defines(defs), enableDebugInfo(debug), warningsAsErrors(warnings) {}
	ShaderInfoBundle() = default;
    std::optional<ShaderInfo> vertexShader;
    std::optional<ShaderInfo> pixelShader;
    std::optional<ShaderInfo> amplificationShader;
    std::optional<ShaderInfo> meshShader;
    std::optional<ShaderInfo> computeShader;

    std::vector<DxcDefine> defines;
    bool enableDebugInfo = false;
    bool warningsAsErrors = true;
};

struct ShaderBundle {
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    Microsoft::WRL::ComPtr<ID3DBlob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
	PipelineResources resourceDescriptorSlots;
	uint64_t resourceIDsHash = 0;
};

class PSOManager {
public:

    static PSOManager& GetInstance();

    void initialize();

    PipelineState GetPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    PipelineState GetPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    PipelineState GetMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    PipelineState GetMeshPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    PipelineState GetPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    PipelineState GetMeshPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    PipelineState GetShadowPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    PipelineState GetShadowMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    PipelineState GetDeferredPSO(UINT psoFlags);

    ComPtr<ID3D12RootSignature> GetRootSignature();
	ComPtr<ID3D12RootSignature> GetComputeRootSignature();
    void ReloadShaders();
    std::vector<DxcDefine> GetShaderDefines(UINT psoFlags);
	ShaderBundle CompileShaders(const ShaderInfoBundle& shaderInfoBundle);
    void GetPreprocessedBlob(
        const std::wstring& filename,
        const std::wstring& entryPoint,
        const std::wstring& target,
        std::vector<DxcDefine> defines,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob);

private:
    struct ShaderCompileOptions
    {
        std::wstring entryPoint;
        std::wstring target;
        std::vector<DxcDefine> defines;
        bool enableDebugInfo = false;
        bool warningsAsErrors = true;
    };

    struct SourceData {
        DxcBuffer                  buffer;
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> blob;
    };

    PSOManager() = default;
    ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12RootSignature> computeRootSignature;
    ComPtr<ID3D12RootSignature> debugRootSignature;
    ComPtr<ID3D12RootSignature> environmentConversionRootSignature;

    std::unordered_map<PSOKey, PipelineState> m_psoCache;
    std::unordered_map<PSOKey, PipelineState> m_PPLLPSOCache;
    std::unordered_map<PSOKey, PipelineState> m_meshPSOCache;
    std::unordered_map<PSOKey, PipelineState> m_meshPPLLPSOCache;

    std::unordered_map<PSOKey, PipelineState> m_prePassPSOCache;
    std::unordered_map<PSOKey, PipelineState> m_meshPrePassPSOCache;

    std::unordered_map<PSOKey, PipelineState> m_shadowPSOCache;
	std::unordered_map<PSOKey, PipelineState> m_shadowMeshPSOCache;

	std::unordered_map<unsigned int, PipelineState> m_deferredPSOCache;

    ComPtr<IDxcUtils> pUtils;
    ComPtr<IDxcCompiler3> pCompiler;
	ComPtr<ID3D12PipelineState> debugPSO;
    ComPtr<ID3D12PipelineState> environmentConversionPSO;

    PipelineState CreatePSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    PipelineState CreatePPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    PipelineState CreateMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    PipelineState CreateMeshPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    PipelineState CreatePrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    PipelineState CreateMeshPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    PipelineState CreateShadowPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);
    PipelineState CreateShadowMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe = false);

    PipelineState CreateDeferredPSO(UINT psoFlags);

    void CompileShaderForSlot(
        const std::optional<ShaderInfo>& slot,
        const std::vector<DxcDefine>& defines,
		const DxcBuffer& buffer,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob);
    void CompileShader(const std::wstring& filename, 
        const std::wstring& entryPoint, 
        const std::wstring& target, 
        const DxcBuffer& ppBuffer,
        std::vector<DxcDefine> defines, 
        Microsoft::WRL::ComPtr<ID3DBlob>& shaderBlob);

    void createRootSignature();
    D3D12_BLEND_DESC GetBlendDesc(BlendState blendState);

    void LoadSource(const std::filesystem::path& path, PSOManager::SourceData& sd);

    ComPtr<IDxcIncludeHandler> CreateIncludeHandler();

    std::vector<LPCWSTR> BuildArguments(
        const ShaderCompileOptions& opts,
        const std::filesystem::path& shaderDir);

    ComPtr<IDxcResult> InvokeCompile(
        const DxcBuffer& srcBuffer,
        std::vector<LPCWSTR>& arguments,
        IDxcIncludeHandler* includeHandler);

    ComPtr<IDxcBlob> ExtractObject(
        IDxcResult* result,
        const std::wstring& filename,
        bool writeDebugArtifacts);

    void WriteDebugArtifacts(
        IDxcResult* result,
        const std::filesystem::path& outDir,
        const std::wstring& baseName);

    template<typename BlobT>
    void PreprocessShaderSlot(
        const std::optional<ShaderInfo>& slot,
        const std::vector<DxcDefine>& defines,
        Microsoft::WRL::ComPtr<BlobT>& outBlob,
        DxcBuffer& outBuf)
    {
        if (!slot)
            return;

        GetPreprocessedBlob(
            slot->filename,
            slot->entryPoint,
            slot->target,
            defines,
            outBlob
        );

        outBuf.Ptr = outBlob->GetBufferPointer();
        outBuf.Size = outBlob->GetBufferSize();
        outBuf.Encoding = 0;
    }

};

inline PSOManager& PSOManager::GetInstance() {
    static PSOManager instance;
    return instance;
}