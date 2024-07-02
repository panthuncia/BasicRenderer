#include "utilities.h"

#include <wrl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include "MeshUtilities.h"

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

std::shared_ptr<RenderableObject> RenderableFromData(MeshData meshData, std::string name) {
    std::vector<Mesh> meshes;

    for (auto geom : meshData.geometries) {
        TangentBitangent tanbit;
        bool hasTexcoords = !geom.texcoords.empty();
        bool hasJoints = !geom.joints.empty() && !geom.weights.empty();
        bool hasTangents = false;

        if (hasTexcoords) {
            if (!geom.indices.empty()) {
                std::vector<XMFLOAT3>& xmfloat3Positions = *reinterpret_cast<std::vector<XMFLOAT3>*>(&geom.positions);
                std::vector<XMFLOAT3>& xmfloat3Normals = *reinterpret_cast<std::vector<XMFLOAT3>*>(&geom.normals);
                std::vector<XMFLOAT2>& xmfloat2Texcoords = *reinterpret_cast<std::vector<XMFLOAT2>*>(&geom.texcoords);

                tanbit = calculateTangentsBitangentsIndexed(xmfloat3Positions, xmfloat3Normals, xmfloat2Texcoords, geom.indices);
                hasTangents = true;
            }
        }

        std::vector<Vertex> vertices;
        for (size_t i = 0; i < geom.positions.size() / 3; ++i) {
            XMFLOAT3 position = XMFLOAT3(geom.positions[i * 3], geom.positions[i * 3 + 1], geom.positions[i * 3 + 2]);
            XMFLOAT3 normal = XMFLOAT3(geom.normals[i * 3], geom.normals[i * 3 + 1], geom.normals[i * 3 + 2]);
            XMFLOAT2 texcoord = hasTexcoords ? XMFLOAT2(geom.texcoords[i * 2], geom.texcoords[i * 2 + 1]) : XMFLOAT2(0.0f, 0.0f);
            XMFLOAT3 tangent = hasTangents ? tanbit.tangents[i] : XMFLOAT3(0.0f, 0.0f, 0.0f);
            XMFLOAT3 bitangent = hasTangents ? tanbit.bitangents[i] : XMFLOAT3(0.0f, 0.0f, 0.0f);
            XMFLOAT4 joints = hasJoints ? XMFLOAT4(geom.joints[i * 4], geom.joints[i * 4 + 1], geom.joints[i * 4 + 2], geom.joints[i * 4 + 3]) : XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
            XMFLOAT4 weights = hasJoints ? XMFLOAT4(geom.weights[i * 4], geom.weights[i * 4 + 1], geom.weights[i * 4 + 2], geom.weights[i * 4 + 3]) : XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

            if (hasJoints && hasTangents) {
                vertices.push_back(VertexNormalMappedSkinned{ position, normal, texcoord, tangent, bitangent, joints, weights });
            }
            else if (hasJoints) {
                vertices.push_back(VertexSkinned{ position, normal, texcoord, joints, weights });
            }
            else if (hasTangents) {
                vertices.push_back(VertexNormalMapped{ position, normal, texcoord, tangent, bitangent });
            }
            else if (hasTexcoords) {
                vertices.push_back(VertexTextured{ position, normal, texcoord });
            }
            else {
                vertices.push_back(VertexBasic{ position, normal });
            }
        }

        Mesh mesh = Mesh(vertices, geom.indices, geom.material);
        meshes.push_back(std::move(mesh));
    }

    return std::make_shared<RenderableObject>(name, meshes);
}