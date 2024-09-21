#include "utilities.h"

#include <wrl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <algorithm>
#include <codecvt>
#include "MeshUtilities.h"
#include "PSOFlags.h"
#include "DirectX/d3dx12.h"
#include "DefaultDirection.h"
#include "Sampler.h"

void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        // Print the error code for debugging purposes
        std::cerr << "HRESULT failed with error code: " << std::hex << hr << std::endl;
        throw std::runtime_error("HRESULT failed");
    }
}

std::shared_ptr<RenderableObject> RenderableFromData(MeshData meshData, std::wstring name) {
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
	auto buffer = PixelBuffer::CreateFromImage(img.data, img.width, img.height, img.channels, false);
    auto sampler = Sampler::GetDefaultSampler();
    return std::make_shared<Texture>(buffer, sampler);
}

DirectX::XMMATRIX createDirectionalLightViewMatrix(XMVECTOR lightDir, XMVECTOR center) {
    auto mat = XMMatrixLookToRH(center, lightDir, XMVectorSet(0, 1, 0, 1));
    return mat;
}

void CalculateFrustumCorners(const Camera& camera, float nearPlane, float farPlane, std::array<XMVECTOR, 8>& corners) {
    float fovY = camera.GetFOV();
    float aspectRatio = camera.GetAspect();

    // Calculate the dimensions of the near and far planes
    float tanHalfFovy = tanf(fovY / 2.0f);
    float nearHeight = 2.0f * tanHalfFovy * nearPlane;
    float nearWidth = nearHeight * aspectRatio;

    float farHeight = 2.0f * tanHalfFovy * farPlane;
    float farWidth = farHeight * aspectRatio;

    // Get camera basis vectors
    XMVECTOR camPos = XMLoadFloat3(&camera.transform.pos);
    XMVECTOR camDir = camera.transform.GetForward();
    XMVECTOR camUp = camera.transform.GetUp();
    XMVECTOR camRight = XMVector3Cross(camDir, camUp);

    XMVECTOR nearCenter = camPos + camDir * nearPlane;
    XMVECTOR farCenter = camPos + camDir * farPlane;

    // Near plane
    corners[0] = nearCenter + (camUp * (nearHeight / 2.0f)) - (camRight * (nearWidth / 2.0f)); // Top-left
    corners[1] = nearCenter + (camUp * (nearHeight / 2.0f)) + (camRight * (nearWidth / 2.0f)); // Top-right
    corners[2] = nearCenter - (camUp * (nearHeight / 2.0f)) - (camRight * (nearWidth / 2.0f)); // Bottom-left
    corners[3] = nearCenter - (camUp * (nearHeight / 2.0f)) + (camRight * (nearWidth / 2.0f)); // Bottom-right

    // Far plane
    corners[4] = farCenter + (camUp * (farHeight / 2.0f)) - (camRight * (farWidth / 2.0f)); // Top-left
    corners[5] = farCenter + (camUp * (farHeight / 2.0f)) + (camRight * (farWidth / 2.0f)); // Top-right
    corners[6] = farCenter - (camUp * (farHeight / 2.0f)) - (camRight * (farWidth / 2.0f)); // Bottom-left
    corners[7] = farCenter - (camUp * (farHeight / 2.0f)) + (camRight * (farWidth / 2.0f)); // Bottom-right
}

std::vector<Cascade> setupCascades(int numCascades, Light& light, Camera& camera, const std::vector<float>& cascadeSplits) {
    std::vector<Cascade> cascades;

    XMVECTOR lightDir = light.GetLightDir();
    XMVECTOR lightPos = XMVectorZero(); // For directional lights, position can be zero
    XMVECTOR lightUp = XMVectorSet(0, 1, 0, 0);
    XMMATRIX lightViewMatrix = XMMatrixLookToRH(lightPos, lightDir, lightUp);

    float prevSplitDist = camera.GetNear();

    for (int i = 0; i < numCascades; ++i)
    {
        float splitDist = cascadeSplits[i];

        // Calculate frustum corners for the cascade
        std::array<XMVECTOR, 8> frustumCornersWorld;
        CalculateFrustumCorners(camera, prevSplitDist, splitDist, frustumCornersWorld);

        // Transform corners to light space
        std::array<XMVECTOR, 8> frustumCornersLightSpace;
        for (int j = 0; j < 8; ++j)
        {
            frustumCornersLightSpace[j] = XMVector3Transform(frustumCornersWorld[j], lightViewMatrix);
        }

        // Compute the bounding box
        XMVECTOR minPoint = frustumCornersLightSpace[0];
        XMVECTOR maxPoint = frustumCornersLightSpace[0];

        for (int j = 1; j < 8; ++j)
        {
            minPoint = XMVectorMin(minPoint, frustumCornersLightSpace[j]);
            maxPoint = XMVectorMax(maxPoint, frustumCornersLightSpace[j]);
        }

        float minX = XMVectorGetX(minPoint);
        float maxX = XMVectorGetX(maxPoint);
        float minY = XMVectorGetY(minPoint);
        float maxY = XMVectorGetY(maxPoint);
        float minZ = XMVectorGetZ(minPoint);
        float maxZ = XMVectorGetZ(maxPoint);

        XMMATRIX orthoMatrix = XMMatrixOrthographicOffCenterRH(minX, maxX, minY, maxY, minZ - 20.0f, maxZ + 20.0f);

        cascades.push_back({ splitDist, orthoMatrix, lightViewMatrix });

        prevSplitDist = splitDist;
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

std::wstring to_wstring(const std::string& stringToConvert) {
    std::wstring wideString =
        std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(stringToConvert);
    return wideString;
}

std::wstring s2ws(const std::string& str) {
    using convert_typeX = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.from_bytes(str);
}


std::string ws2s(const std::wstring& wstr) {
    using convert_typeX = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.to_bytes(wstr);
}