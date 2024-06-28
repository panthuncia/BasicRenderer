#include "utilities.h"

#include <wrl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <stdexcept>

void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        // Print the error code for debugging purposes
        std::cerr << "HRESULT failed with error code: " << std::hex << hr << std::endl;
        throw std::runtime_error("HRESULT failed");
    }
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& target) {
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint.c_str(), target.c_str(), 0, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Shader Compilation Error: " << static_cast<char*>(errorBlob->GetBufferPointer()) << std::endl;
        }
        throw std::runtime_error("Shader compilation failed");
    }

    return shaderBlob;
}