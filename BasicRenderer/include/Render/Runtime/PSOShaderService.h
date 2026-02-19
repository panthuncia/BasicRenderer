#pragma once

#include "Render/Runtime/IShaderService.h"

namespace rg::runtime {

class PsoShaderService final : public IShaderService {
public:
    void Initialize() override;
    ShaderProgramCompileResult CompileProgram(const ShaderProgramCompileRequest& request) override;
    ShaderLibraryCompileResult CompileLibrary(const ShaderLibraryCompileRequest& request) override;
    std::vector<uint8_t> PreprocessShader(const ShaderPreprocessRequest& request) override;
    void Cleanup() override;
};

}
