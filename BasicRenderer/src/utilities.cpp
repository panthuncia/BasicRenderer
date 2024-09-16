#include "utilities.h"

#include <wrl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <algorithm>
#include "MeshUtilities.h"
#include "PSOFlags.h"
#include "DirectX/d3dx12.h"
#include "DefaultDirection.h"

void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        // Print the error code for debugging purposes
        std::cerr << "HRESULT failed with error code: " << std::hex << hr << std::endl;
        throw std::runtime_error("HRESULT failed");
    }
}

std::shared_ptr<RenderableObject> RenderableFromData(MeshData meshData, std::string name) {
    std::vector<Mesh> meshes;

    for (auto geom : meshData.geometries) {
        TangentBitangent tanbit;
        bool hasTexcoords = !geom.texcoords.empty();
        bool hasJoints = !geom.joints.empty() && !geom.weights.empty();
        bool hasTangents = false;

        if (geom.material->m_psoFlags & PSOFlags::NORMAL_MAP) {
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
            XMUINT4 joints = hasJoints ? XMUINT4(geom.joints[i * 4], geom.joints[i * 4 + 1], geom.joints[i * 4 + 2], geom.joints[i * 4 + 3]) : XMUINT4(0, 0, 0, 0);
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

        Mesh mesh = Mesh(vertices, geom.indices, geom.material, hasJoints);
        meshes.push_back(std::move(mesh));
    }

    return std::make_shared<RenderableObject>(name, meshes);
}

XMMATRIX RemoveScalingFromMatrix(XMMATRIX& initialMatrix) {
    XMVECTOR translation = initialMatrix.r[3];
    XMVECTOR right = initialMatrix.r[0];
    XMVECTOR up = initialMatrix.r[1];
    XMVECTOR forward = initialMatrix.r[2];
    right = XMVector3Normalize(right);
    up = XMVector3NormalizeEst(up);
    forward = XMVector3Normalize(forward);

    XMMATRIX result = XMMatrixIdentity();
    result.r[0] = right;
    result.r[1] = up;
    result.r[2] = forward;
    result.r[3] = translation;

    return result;
}

struct ImageData {
    stbi_uc* data;
    int width;
    int height;
    int channels;

    ~ImageData() {
        if (data) {
            stbi_image_free(data);
        }
    }
};

ImageData loadImage(const char* filename) {
    ImageData img;
    img.data = stbi_load(filename, &img.width, &img.height, &img.channels, 0);
    if (!img.data) {
        throw std::runtime_error("Failed to load image: " + std::string(filename));
    }
    return img;
}

std::shared_ptr<Texture> loadTextureFromFile(const char* filename) {
	ImageData img = loadImage(filename);
    PixelBuffer bufferTest(img.data, img.width, img.height, img.channels, false);
	auto buffer = std::make_shared<PixelBuffer>(img.data, img.width, img.height, img.channels, false);
    auto sampler = Sampler::GetDefaultSampler();
    return std::make_shared<Texture>(buffer, sampler);
}

DirectX::XMMATRIX createDirectionalLightViewMatrix(XMVECTOR lightDir, XMVECTOR center) {
    auto mat = XMMatrixLookAtRH(center, XMVectorAdd(center, lightDir), defaultDirection);
    return mat;
}

std::vector<Cascade> setupCascades(int numCascades, Light& light, Camera& camera, const std::vector<float>& cascadeSplits) {
    std::vector<Cascade> cascades;

    for (int i = 0; i < numCascades; ++i) {
        float size = cascadeSplits[i];
        auto pos = camera.transform.getGlobalPosition();
        XMVECTOR camPos = XMLoadFloat3(&pos);
        XMVECTOR center = XMVectorSet(XMVectorGetX(camPos), 0.0f, XMVectorGetZ(camPos), 0.0f);
        XMMATRIX viewMatrix = createDirectionalLightViewMatrix(light.GetLightDir(), center);
        XMMATRIX orthoMatrix = XMMatrixOrthographicRH(size, size, -20.0f, 100.0f);

        cascades.push_back({ size, center, orthoMatrix, viewMatrix });
    }

    return cascades;
}

std::vector<float> calculateCascadeSplits(int numCascades, float zNear, float zFar, float maxDist, float lambda) {
    std::vector<float> splits(numCascades);
    float end = (std::min)(zFar, maxDist);
    float logNear = std::log(zNear);
    float logFar = std::log(end);
    float logRange = logFar - logNear;
    float uniformRange = end - zNear;

    for (int i = 0; i < numCascades; i++) {
        float p = (i + 1.0f) / numCascades;
        float logSplit = std::exp(logNear + logRange * p);
        float uniformSplit = zNear + uniformRange * p;
        splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }

    return splits;
}