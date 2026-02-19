#include "Render/Runtime/PSOShaderService.h"

#include "Managers/Singletons/PSOManager.h"

namespace rg::runtime {

namespace {

std::vector<DxcDefine> ConvertMacros(const std::vector<ShaderMacro>& macros) {
    std::vector<DxcDefine> defines;
    defines.reserve(macros.size());
    for (const auto& macro : macros) {
        DxcDefine d{};
        d.Name = macro.name.c_str();
        d.Value = macro.value.empty() ? nullptr : macro.value.c_str();
        defines.push_back(d);
    }
    return defines;
}

std::optional<ShaderInfo> ConvertShaderInfo(const std::optional<ShaderEntryPointDesc>& slot) {
    if (!slot.has_value()) {
        return std::nullopt;
    }
    return ShaderInfo{ slot->filename, slot->entryPoint, slot->target };
}

std::vector<uint8_t> BlobToBytes(const Microsoft::WRL::ComPtr<ID3DBlob>& blob) {
    if (!blob) {
        return {};
    }

    const auto* begin = static_cast<const uint8_t*>(blob->GetBufferPointer());
    const auto* end = begin + blob->GetBufferSize();
    return std::vector<uint8_t>(begin, end);
}

}

void PsoShaderService::Initialize() {
}

ShaderProgramCompileResult PsoShaderService::CompileProgram(const ShaderProgramCompileRequest& request) {
    ShaderInfoBundle bundle;
    bundle.vertexShader = ConvertShaderInfo(request.vertexShader);
    bundle.pixelShader = ConvertShaderInfo(request.pixelShader);
    bundle.amplificationShader = ConvertShaderInfo(request.amplificationShader);
    bundle.meshShader = ConvertShaderInfo(request.meshShader);
    bundle.computeShader = ConvertShaderInfo(request.computeShader);
    bundle.defines = ConvertMacros(request.macros);
    bundle.enableDebugInfo = request.enableDebugInfo;
    bundle.warningsAsErrors = request.warningsAsErrors;

    auto compiled = PSOManager::GetInstance().CompileShaders(bundle);

    ShaderProgramCompileResult result;
    result.vertexShader = BlobToBytes(compiled.vertexShader);
    result.pixelShader = BlobToBytes(compiled.pixelShader);
    result.amplificationShader = BlobToBytes(compiled.amplificationShader);
    result.meshShader = BlobToBytes(compiled.meshShader);
    result.computeShader = BlobToBytes(compiled.computeShader);
    result.resourceDescriptorSlots = std::move(compiled.resourceDescriptorSlots);
    result.resourceIDsHash = compiled.resourceIDsHash;
    return result;
}

ShaderLibraryCompileResult PsoShaderService::CompileLibrary(const ShaderLibraryCompileRequest& request) {
    ShaderLibraryInfo libraryInfo{ request.filename, request.target };
    auto compiled = PSOManager::GetInstance().CompileShaderLibrary(libraryInfo, ConvertMacros(request.macros));

    ShaderLibraryCompileResult result;
    result.library = BlobToBytes(compiled.libraryBlob);
    result.resourceDescriptorSlots = std::move(compiled.resourceDescriptorSlots);
    result.resourceIDsHash = compiled.resourceIDsHash;
    return result;
}

std::vector<uint8_t> PsoShaderService::PreprocessShader(const ShaderPreprocessRequest& request) {
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    PSOManager::GetInstance().GetPreprocessedBlob(
        request.filename,
        request.entryPoint,
        request.target,
        ConvertMacros(request.macros),
        blob);
    return BlobToBytes(blob);
}

void PsoShaderService::Cleanup() {
}

}
