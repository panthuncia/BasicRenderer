#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Render/PipelineState.h"

namespace rg::runtime {

struct ShaderMacro {
    std::wstring name;
    std::wstring value;
};

struct ShaderEntryPointDesc {
    std::wstring filename;
    std::wstring entryPoint;
    std::wstring target;
};

struct ShaderProgramCompileRequest {
    std::optional<ShaderEntryPointDesc> vertexShader;
    std::optional<ShaderEntryPointDesc> pixelShader;
    std::optional<ShaderEntryPointDesc> amplificationShader;
    std::optional<ShaderEntryPointDesc> meshShader;
    std::optional<ShaderEntryPointDesc> computeShader;
    std::vector<ShaderMacro> macros;
    bool enableDebugInfo = false;
    bool warningsAsErrors = true;
};

struct ShaderProgramCompileResult {
    std::vector<uint8_t> vertexShader;
    std::vector<uint8_t> pixelShader;
    std::vector<uint8_t> amplificationShader;
    std::vector<uint8_t> meshShader;
    std::vector<uint8_t> computeShader;
    PipelineResources resourceDescriptorSlots;
    uint64_t resourceIDsHash = 0;
};

struct ShaderLibraryCompileRequest {
    std::wstring filename;
    std::wstring target;
    std::vector<ShaderMacro> macros;
};

struct ShaderLibraryCompileResult {
    std::vector<uint8_t> library;
    PipelineResources resourceDescriptorSlots;
    uint64_t resourceIDsHash = 0;
};

struct ShaderPreprocessRequest {
    std::wstring filename;
    std::wstring entryPoint;
    std::wstring target;
    std::vector<ShaderMacro> macros;
};

class IShaderService {
public:
    virtual ~IShaderService() = default;

    virtual void Initialize() = 0;
    virtual ShaderProgramCompileResult CompileProgram(const ShaderProgramCompileRequest& request) = 0;
    virtual ShaderLibraryCompileResult CompileLibrary(const ShaderLibraryCompileRequest& request) = 0;
    virtual std::vector<uint8_t> PreprocessShader(const ShaderPreprocessRequest& request) = 0;
    virtual void Cleanup() = 0;
};

std::shared_ptr<IShaderService> CreateDefaultShaderService();

}
