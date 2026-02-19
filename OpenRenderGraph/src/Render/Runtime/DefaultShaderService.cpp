#include "Render/Runtime/IShaderService.h"

#include <stdexcept>

namespace rg::runtime {

namespace {
class DefaultShaderService final : public IShaderService {
public:
    void Initialize() override {}

    ShaderProgramCompileResult CompileProgram(const ShaderProgramCompileRequest&) override {
        throw std::runtime_error("Shader service not configured: CompileProgram is unavailable");
    }

    ShaderLibraryCompileResult CompileLibrary(const ShaderLibraryCompileRequest&) override {
        throw std::runtime_error("Shader service not configured: CompileLibrary is unavailable");
    }

    std::vector<uint8_t> PreprocessShader(const ShaderPreprocessRequest&) override {
        throw std::runtime_error("Shader service not configured: PreprocessShader is unavailable");
    }

    void Cleanup() override {}
};
}

std::shared_ptr<IShaderService> CreateDefaultShaderService() {
    return std::make_shared<DefaultShaderService>();
}

}
