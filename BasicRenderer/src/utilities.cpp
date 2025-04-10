#include "utilities.h"

#include <wrl.h>
#include <stdexcept>
#include <algorithm>
#include <codecvt>
#include <DirectXTex.h>
#include <future>
#include <functional>
#include <filesystem>
#include <fstream>
#define _USE_MATH_DEFINES
#include <math.h>
#include <optional>

#include "PSOFlags.h"
#include "DirectX/d3dx12.h"
#include "DefaultDirection.h"
#include "Sampler.h"
#include "DescriptorHeap.h"
#include "ReadbackRequest.h"
#include "Material.h"
#include "SettingsManager.h"
#include "Vertex.h"
#include "MaterialFlags.h"
#include "Mesh.h"
#include "Components.h"

void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        // Print the error code for debugging purposes
        std::cerr << "HRESULT failed with error code: " << std::hex << hr << std::endl;
        throw std::runtime_error("HRESULT failed");
    }
}

std::shared_ptr<Mesh> MeshFromData(const MeshData& meshData, std::wstring name) {
    bool hasTexcoords = !meshData.texcoords.empty();
    bool hasJoints = !meshData.joints.empty() && !meshData.weights.empty();
    unsigned int materialFlags = meshData.material->m_materialData.materialFlags;

    std::unique_ptr<std::vector<std::byte>> rawData = std::make_unique<std::vector<std::byte>>();
    unsigned int numVertices = meshData.positions.size() / 3;
    // position,        normal,            texcoord
    uint8_t vertexSize = sizeof(XMFLOAT3) + sizeof(XMFLOAT3) + (hasTexcoords ? sizeof(XMFLOAT2) : 0);
    rawData->resize(numVertices * vertexSize);

    for (unsigned int i = 0; i < numVertices; i++) {
        size_t baseOffset = i * vertexSize;
        memcpy(rawData->data() + baseOffset, &meshData.positions[i * 3], sizeof(XMFLOAT3));
        size_t offset = sizeof(XMFLOAT3);
        memcpy(rawData->data() + baseOffset + offset, &meshData.normals[i * 3], sizeof(XMFLOAT3));
        offset += sizeof(XMFLOAT3);
        if (hasTexcoords) {
            memcpy(rawData->data() + baseOffset + offset, &meshData.texcoords[i * 2], sizeof(XMFLOAT2));
            offset += sizeof(XMFLOAT2);
        }
    }
    // position,       normal            joints,           weights
    unsigned int skinningVertexSize = sizeof(XMFLOAT3) + sizeof(XMFLOAT3)  + sizeof(XMUINT4) + sizeof(XMFLOAT4);
    std::unique_ptr<std::vector<std::byte>> skinningData = std::make_unique<std::vector<std::byte>>();
    if (hasJoints) {
        skinningData->resize(numVertices * skinningVertexSize);
        for (unsigned int i = 0; i < numVertices; i++) {
            size_t baseOffset = i * skinningVertexSize;
            memcpy(skinningData->data() + baseOffset, &meshData.positions[i * 3], sizeof(XMFLOAT3));
            size_t offset = sizeof(XMFLOAT3);
            memcpy(skinningData->data() + baseOffset + offset, &meshData.normals[i * 3], sizeof(XMFLOAT3));
            offset += sizeof(XMFLOAT3);
            memcpy(skinningData->data() + baseOffset + offset, &meshData.joints[i * 4], sizeof(XMUINT4));
            offset += sizeof(XMUINT4);
            memcpy(skinningData->data() + baseOffset + offset, &meshData.weights[i * 4], sizeof(XMFLOAT4));
        }
    }

    return Mesh::CreateShared(std::move(rawData), vertexSize, std::move(skinningData), skinningVertexSize, meshData.indices, meshData.material, meshData.flags);
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

std::shared_ptr<Texture> loadTextureFromFileSTBI(std::string filename, std::shared_ptr<Sampler> sampler) {
	ImageData img = loadImage(filename.c_str());
    // Determine DXGI_FORMAT based on number of channels
	DXGI_FORMAT format;
    switch (img.channels) {
        case 1:
            format = DXGI_FORMAT_R8_UNORM;
            break;
        case 2:
            format = DXGI_FORMAT_R8G8_UNORM;
            break;
        case 3:
        case 4:
            format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA
            break;
		default:
			throw std::runtime_error("Unsupported channel count");
    }

	ImageDimensions dim;
	dim.width = img.width;
	dim.height = img.height;
	dim.rowPitch = img.width * img.channels;
	dim.slicePitch = img.height * img.width * img.channels;

    TextureDescription desc;
	desc.imageDimensions.push_back(dim);
	desc.channels = img.channels;
	desc.format = format;
	auto buffer = PixelBuffer::Create(desc, { img.data });

    if (!sampler) {
        sampler = Sampler::GetDefaultSampler();
    }
    return std::make_shared<Texture>(buffer, sampler);
}

std::shared_ptr<Texture> loadTextureFromFileDXT(std::wstring ddsFilePath, std::shared_ptr<Sampler> sampler) {
	DirectX::ScratchImage image;
	DirectX::TexMetadata metadata;
	HRESULT hr = DirectX::LoadFromDDSFile(ddsFilePath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);

	if (FAILED(hr)) {
		throw std::runtime_error("Failed to load DDS texture: " + ws2s(ddsFilePath));
	}
	// Extract the first mip level
	const DirectX::Image* img = image.GetImage(0, 0, 0); // mip 0, face 0, slice 0
	ImageDimensions dim;
	dim.width = metadata.width;
	dim.height = metadata.height;
	dim.rowPitch = img->rowPitch;
	dim.slicePitch = img->slicePitch;

	TextureDescription desc;
	desc.imageDimensions.push_back(dim);
	desc.channels = 4;
	desc.format = metadata.format;
    desc.generateMipMaps = false;// metadata.mipLevels != 1;
	auto buffer = PixelBuffer::Create(desc, { img->pixels });
	if (!sampler) {
		sampler = Sampler::GetDefaultSampler();
	}
    auto texture = std::make_shared<Texture>(buffer, sampler);
	texture->SetFilepath(ws2s(ddsFilePath));
	texture->SetAlphaIsAllOpaque(image.IsAlphaAllOpaque());
	return texture;
}

std::shared_ptr<Texture> loadCubemapFromFile(const char* topPath, const char* bottomPath, const char* leftPath, const char* rightPath, const char* frontPath, const char* backPath) {
    ImageData top = loadImage(topPath);
	ImageData bottom = loadImage(bottomPath);
	ImageData left = loadImage(leftPath);
	ImageData right = loadImage(rightPath);
	ImageData front = loadImage(frontPath);
	ImageData back = loadImage(backPath);


	ImageDimensions dim;
	dim.width = top.width;
	dim.height = top.height;
	dim.rowPitch = top.width * top.channels;
	dim.slicePitch = dim.rowPitch * top.height;

	TextureDescription desc;
	desc.imageDimensions.push_back(dim);
	desc.channels = top.channels;
	desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.isCubemap = true;

    auto buffer = PixelBuffer::Create(desc, {right.data, left.data, top.data, bottom.data, front.data, back.data });
    auto sampler = Sampler::GetDefaultSampler();
    return std::make_shared<Texture>(buffer, sampler);
}

std::shared_ptr<Texture> loadCubemapFromFile(std::wstring ddsFilePath) {
    DirectX::ScratchImage image;
    DirectX::TexMetadata metadata;
    HRESULT hr = DirectX::LoadFromDDSFile(ddsFilePath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
    
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to load DDS cubemap: " + ws2s(ddsFilePath));
    }

    if (!(metadata.miscFlags & DirectX::TEX_MISC_TEXTURECUBE)) {
        throw std::runtime_error("The DDS file is not a cubemap: " + ws2s(ddsFilePath));
    }

    // Extract cubemap faces and create a PixelBuffer from them
    TextureDescription desc;

    std::vector<const stbi_uc*> faces = {};
    for (size_t face = 0; face < 6; ++face) {
        for (size_t mip = 0; mip < metadata.mipLevels; ++mip) {
            const DirectX::Image* img = image.GetImage(mip, face, 0); // mip 0, face i, slice 0
            faces.push_back(img->pixels);
			ImageDimensions dim;
			dim.width = img->width;
			dim.height = img->height;
			dim.rowPitch = img->rowPitch;
			dim.slicePitch = img->slicePitch;
			desc.imageDimensions.push_back(dim);
        }
    }
	desc.channels = 4;
    desc.format = metadata.format;
	desc.isCubemap = true;
    if (metadata.mipLevels != 1) {
		desc.generateMipMaps = true;
    }

	auto buffer = PixelBuffer::Create(desc, faces);

    auto sampler = Sampler::GetDefaultSampler();
    return std::make_shared<Texture>(buffer, sampler);
}

DirectX::XMMATRIX createDirectionalLightViewMatrix(XMVECTOR lightDir, XMVECTOR center) {
    auto mat = XMMatrixLookToRH(center, lightDir, XMVectorSet(0, 1, 0, 1));
    return mat;
}

void CalculateFrustumCorners(const DirectX::XMVECTOR& camPos, const DirectX::XMVECTOR& camDir, const DirectX::XMVECTOR& camUp, float nearPlane, float farPlane, float fovY, float aspectRatio, std::array<XMVECTOR, 8>& corners) {

    // Calculate the dimensions of the near and far planes
    float tanHalfFovy = tanf(fovY / 2.0f);
    float nearHeight = 2.0f * tanHalfFovy * nearPlane;
    float nearWidth = nearHeight * aspectRatio;

    float farHeight = 2.0f * tanHalfFovy * farPlane;
    float farWidth = farHeight * aspectRatio;

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

std::vector<Cascade> setupCascades(
    int numCascades, 
    const DirectX::XMVECTOR& lightDir, 
    const DirectX::XMVECTOR& camPos, 
    const DirectX::XMVECTOR& camDir, 
    const DirectX::XMVECTOR& camUp, 
    float nearPlane, 
    float fovY, 
    float aspectRatio, 
    const std::vector<float>& cascadeSplits)
{
    using namespace DirectX;
    std::vector<Cascade> cascades;
    cascades.reserve(numCascades);

    // Compute the camera's right vector
    XMVECTOR camRight = XMVector3Normalize(XMVector3Cross(XMVector3Normalize(camDir), XMVector3Normalize(camUp)));

    // Loop over cascades.
    for (int i = 0; i < numCascades; ++i)
    {
        // Determine the near and far distances for this cascade
        float cascadeNear = (i == 0) ? nearPlane : cascadeSplits[i - 1];
        float cascadeFar  = cascadeSplits[i];

        // Compute the center of the near and far planes
        XMVECTOR nearCenter = camPos + camDir * cascadeNear;
        XMVECTOR farCenter  = camPos + camDir * cascadeFar;

        // Calculate half-heights and half-widths at the near and far planes
        float tanFov = tanf(fovY * 0.5f);
        float nearHeight = tanFov * cascadeNear;
        float nearWidth  = nearHeight * aspectRatio;
        float farHeight  = tanFov * cascadeFar;
        float farWidth   = farHeight * aspectRatio;

        // Compute the 8 corners of the cascade frustum in world space
        // Near plane corners
        XMVECTOR nearTopLeft     = nearCenter + camUp * nearHeight - camRight * nearWidth;
        XMVECTOR nearTopRight    = nearCenter + camUp * nearHeight + camRight * nearWidth;
        XMVECTOR nearBottomLeft  = nearCenter - camUp * nearHeight - camRight * nearWidth;
        XMVECTOR nearBottomRight = nearCenter - camUp * nearHeight + camRight * nearWidth;
        // Far plane corners
        XMVECTOR farTopLeft     = farCenter + camUp * farHeight - camRight * farWidth;
        XMVECTOR farTopRight    = farCenter + camUp * farHeight + camRight * farWidth;
        XMVECTOR farBottomLeft  = farCenter - camUp * farHeight - camRight * farWidth;
        XMVECTOR farBottomRight = farCenter - camUp * farHeight + camRight * farWidth;

        // Collect all eight frustum corners
        XMVECTOR frustumCorners[8] = {
            nearTopLeft, nearTopRight, nearBottomLeft, nearBottomRight,
            farTopLeft, farTopRight, farBottomLeft, farBottomRight
        };

        // Compute the centroid of the frustum corners
        XMVECTOR frustumCenter = XMVectorZero();
        for (int j = 0; j < 8; ++j)
        {
            frustumCenter = XMVectorAdd(frustumCenter, frustumCorners[j]);
        }
        frustumCenter = XMVectorScale(frustumCenter, 1.0f / 8.0f);

        // Determine the radius of a sphere that bounds all frustum corners
        float radius = 0.0f;
        for (int j = 0; j < 8; ++j)
        {
            float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(frustumCorners[j], frustumCenter)));
            radius = std::max(radius, distance);
        }
        // Quantize the radius to reduce shimmering
        radius = ceilf(radius * 16.0f) / 16.0f;

        // Position the light so that it covers the cascade bounding sphere
        // The light position is shifted back along the light direction
        XMVECTOR lightPos = frustumCenter - lightDir * radius * 2.0f;
        // Choose a suitable "up" vector for the light (avoid colinearity with the light direction)
        XMVECTOR lightUp = (fabs(XMVectorGetY(lightDir)) > 0.99f) ? XMVectorSet(0, 0, -1, 0) : XMVectorSet(0, 1, 0, 0);
        XMMATRIX lightView = XMMatrixLookAtRH(lightPos, frustumCenter, lightUp);

        // Transform frustum corners into light space
        XMVECTOR lightSpaceCorners[8];
        for (int j = 0; j < 8; ++j)
        {
            lightSpaceCorners[j] = XMVector3TransformCoord(frustumCorners[j], lightView);
        }

        // Compute the axis-aligned bounding box in light space
        XMVECTOR mins = lightSpaceCorners[0];
        XMVECTOR maxs = lightSpaceCorners[0];
        for (int j = 1; j < 8; ++j)
        {
            mins = XMVectorMin(mins, lightSpaceCorners[j]);
            maxs = XMVectorMax(maxs, lightSpaceCorners[j]);
        }
        // Extract the bounds
        float l = XMVectorGetX(mins);
        float r = XMVectorGetX(maxs);
        float b = XMVectorGetY(mins);
        float t = XMVectorGetY(maxs);
        float n = -XMVectorGetZ(maxs); // near
        float f = -XMVectorGetZ(mins); // far

        XMMATRIX lightOrtho = XMMatrixOrthographicOffCenterRH(l, r, b, t, n, f);

        // Prepare the cascade.
        Cascade cascade;
        cascade.size = radius * 2.0f;
        cascade.viewMatrix = lightView;
        cascade.orthoMatrix = lightOrtho;

        // Combine view and projection matrices for plane extraction
        XMMATRIX comboMatrix = XMMatrixMultiply(lightView, lightOrtho);

        // Helper lambda to extract one clipping plane from the combined matrix
        auto ExtractPlane = [&comboMatrix](int planeIndex) -> ClippingPlane {
            // Store the combined matrix into a float4x4 structure
            XMFLOAT4X4 m;
            XMStoreFloat4x4(&m, comboMatrix);
            XMVECTOR planeVec;
            // 0: left, 1: right, 2: bottom, 3: top, 4: near, 5: far
            switch (planeIndex) {
            case 0: // left
                planeVec = XMVectorSet(m._14 + m._11,
                    m._24 + m._21,
                    m._34 + m._31,
                    m._44 + m._41);
                break;
            case 1: // right
                planeVec = XMVectorSet(m._14 - m._11,
                    m._24 - m._21,
                    m._34 - m._31,
                    m._44 - m._41);
                break;
            case 2: // bottom
                planeVec = XMVectorSet(m._14 + m._12,
                    m._24 + m._22,
                    m._34 + m._32,
                    m._44 + m._42);
                break;
            case 3: // top
                planeVec = XMVectorSet(m._14 - m._12,
                    m._24 - m._22,
                    m._34 - m._32,
                    m._44 - m._42);
                break;
            case 4: // near
                planeVec = XMVectorSet(m._13,
                    m._23,
                    m._33,
                    m._43);
                break;
            case 5: // far
                planeVec = XMVectorSet(m._14 - m._13,
                    m._24 - m._23,
                    m._34 - m._33,
                    m._44 - m._43);
                break;
            default:
                planeVec = XMVectorZero();
                break;
            }
            // Normalize the plane
            planeVec = XMPlaneNormalize(planeVec);
            ClippingPlane result;
            XMStoreFloat4(&result.plane, planeVec);
            return result;
            };

        // Extract all six clipping planes
        for (int p = 0; p < 6; ++p)
        {
            cascade.frustumPlanes[p] = ExtractPlane(p);
        }

        cascades.push_back(cascade);
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

DXGI_FORMAT DetermineTextureFormat(int channels, bool sRGB, bool isDSV) {
    if (isDSV) {
        return DXGI_FORMAT_R32_TYPELESS;
    }

    switch (channels) {
    case 1:
        return DXGI_FORMAT_R8_UNORM;
    case 3:
    case 4:
        return sRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
        throw std::invalid_argument("Unsupported channel count");
    }
}

CD3DX12_RESOURCE_DESC CreateTextureResourceDesc(
    DXGI_FORMAT format,
    int width,
    int height,
    int arraySize,
    int mipLevels,
    bool isCubemap,
    bool allowRTV,
    bool allowDSV,
    bool allowUAV) {

    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
        format,
        width,
        height,
        isCubemap ? 6 * arraySize : arraySize,
        mipLevels);

    if (allowRTV) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (allowDSV) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	if (allowUAV) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    return desc;
}

ComPtr<ID3D12Resource> CreateCommittedTextureResource(
    ID3D12Device* device,
    const CD3DX12_RESOURCE_DESC& desc,
    D3D12_CLEAR_VALUE* clearValue,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState) {

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(heapType);
    ComPtr<ID3D12Resource> resource;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        clearValue,
        IID_PPV_ARGS(&resource)));

    return resource;
}

ShaderVisibleIndexInfo CreateShaderResourceView(
    ID3D12Device* device,
    ID3D12Resource* resource,
    DXGI_FORMAT format,
    DescriptorHeap* srvHeap,
    int mipLevels,
    bool isCubemap,
    bool isArray,
    int arraySize) {

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = format;

    if (isCubemap) {
        srvDesc.ViewDimension = isArray ? D3D12_SRV_DIMENSION_TEXTURECUBEARRAY : D3D12_SRV_DIMENSION_TEXTURECUBE;
        if (isArray) {
            srvDesc.TextureCubeArray.MipLevels = mipLevels;
            srvDesc.TextureCubeArray.NumCubes = arraySize;
        }
        else {
            srvDesc.TextureCube.MipLevels = mipLevels;
        }
    }
    else {
        srvDesc.ViewDimension = isArray ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
        if (isArray) {
            srvDesc.Texture2DArray.MipLevels = mipLevels;
            srvDesc.Texture2DArray.ArraySize = arraySize;
        }
        else {
            srvDesc.Texture2D.MipLevels = mipLevels;
        }
    }

    UINT descriptorIndex = srvHeap->AllocateDescriptor();
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUHandle(descriptorIndex);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvHeap->GetGPUHandle(descriptorIndex);

    device->CreateShaderResourceView(resource, &srvDesc, cpuHandle);

    ShaderVisibleIndexInfo srvInfo;
    srvInfo.index = descriptorIndex;
    srvInfo.gpuHandle = gpuHandle;

    return srvInfo;
}

ShaderVisibleIndexInfo CreateUnorderedAccessView(
    ID3D12Device* device,
    ID3D12Resource* resource,
    DXGI_FORMAT format,
    DescriptorHeap* uavHeap,
    bool isArray,
    int arraySize,
    int mipSlice,
    int firstArraySlice,
    int planeSlice) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = format;

    // For now, only support Texture2D or Texture2DArray.
    // TODO: consolidate other uav creation into this?
    if (isArray) {
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = mipSlice;
        uavDesc.Texture2DArray.FirstArraySlice = firstArraySlice;
        uavDesc.Texture2DArray.ArraySize = arraySize;
        uavDesc.Texture2DArray.PlaneSlice = planeSlice;
    }
    else {
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = mipSlice;
        uavDesc.Texture2D.PlaneSlice = planeSlice;
    }

    UINT descriptorIndex = uavHeap->AllocateDescriptor();
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = uavHeap->GetCPUHandle(descriptorIndex);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle = uavHeap->GetGPUHandle(descriptorIndex);

	// No counter for texture UAVs
    device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, cpuHandle);

    ShaderVisibleIndexInfo uavInfo;
    uavInfo.index = descriptorIndex;
    uavInfo.gpuHandle = gpuHandle;

    return uavInfo;
}

NonShaderVisibleIndexInfo CreateNonShaderVisibleUnorderedAccessView( // Clear operations need a non-shader visible UAV
    ID3D12Device* device,
    ID3D12Resource* resource,
    DXGI_FORMAT format,
    DescriptorHeap* uavHeap,
    bool isArray,
    int arraySize,
    int mipSlice,
    int firstArraySlice,
    int planeSlice) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = format;

    if (isArray) {
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = mipSlice;
        uavDesc.Texture2DArray.FirstArraySlice = firstArraySlice;
        uavDesc.Texture2DArray.ArraySize = arraySize;
        uavDesc.Texture2DArray.PlaneSlice = planeSlice;
    }
    else {
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = mipSlice;
        uavDesc.Texture2D.PlaneSlice = planeSlice;
    }

    UINT descriptorIndex = uavHeap->AllocateDescriptor();
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = uavHeap->GetCPUHandle(descriptorIndex);

    // No counter for texture UAVs
    device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, cpuHandle);

    NonShaderVisibleIndexInfo uavInfo;
    uavInfo.index = descriptorIndex;
	uavInfo.cpuHandle = cpuHandle;

    return uavInfo;
}

std::vector<NonShaderVisibleIndexInfo> CreateRenderTargetViews(
    ID3D12Device* device,
    ID3D12Resource* resource,
    DXGI_FORMAT format,
    DescriptorHeap* rtvHeap,
    bool isCubemap,
    bool isArray,
    int arraySize,
    int mipLevels)
{
    std::vector<NonShaderVisibleIndexInfo> rtvInfos;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtvDesc.Texture2DArray.PlaneSlice = 0;
    rtvDesc.Texture2DArray.ArraySize = 1;

    int totalSlices = isCubemap ? 6 * arraySize : arraySize;

    for (int mip = 0; mip < mipLevels; ++mip) {
        rtvDesc.Texture2DArray.MipSlice = mip;  // Set the MipSlice for each mip level

        for (int slice = 0; slice < totalSlices; ++slice) {
            rtvDesc.Texture2DArray.FirstArraySlice = slice;

            UINT descriptorIndex = rtvHeap->AllocateDescriptor();
            CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = rtvHeap->GetCPUHandle(descriptorIndex);

            device->CreateRenderTargetView(resource, &rtvDesc, cpuHandle);

            NonShaderVisibleIndexInfo rtvInfo;
            rtvInfo.index = descriptorIndex;
            rtvInfo.cpuHandle = cpuHandle;
            rtvInfos.push_back(rtvInfo);
        }
    }

    return rtvInfos;
}

std::vector<NonShaderVisibleIndexInfo> CreateDepthStencilViews(
    ID3D12Device* device,
    ID3D12Resource* resource,
    DescriptorHeap* dsvHeap,
    bool isCubemap,
    bool isArray,
    int arraySize) {

    std::vector<NonShaderVisibleIndexInfo> dsvInfos;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.MipSlice = 0;
    dsvDesc.Texture2DArray.ArraySize = 1;

    int totalSlices = isCubemap ? 6 * arraySize : arraySize;

    for (int slice = 0; slice < totalSlices; ++slice) {
        dsvDesc.Texture2DArray.FirstArraySlice = slice;

        UINT descriptorIndex = dsvHeap->AllocateDescriptor();
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = dsvHeap->GetCPUHandle(descriptorIndex);

        device->CreateDepthStencilView(resource, &dsvDesc, cpuHandle);

        NonShaderVisibleIndexInfo dsvInfo;
        dsvInfo.index = descriptorIndex;
        dsvInfo.cpuHandle = cpuHandle;
        dsvInfos.push_back(dsvInfo);
    }

    return dsvInfos;
}

std::vector<stbi_uc> ExpandImageData(const stbi_uc* image, int width, int height) {
    std::vector<stbi_uc> expandedData(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        expandedData[i * 4] = image[i * 3];         // R
        expandedData[i * 4 + 1] = image[i * 3 + 1]; // G
        expandedData[i * 4 + 2] = image[i * 3 + 2]; // B
        expandedData[i * 4 + 3] = 255;              // A
    }
    return expandedData;
}

std::array<DirectX::XMMATRIX, 6> GetCubemapViewMatrices(XMFLOAT3 pos) {
    // Define directions and up vectors for the six faces of the cubemap
    // Directions for the cubemap faces
    XMVECTOR targets[6] = {
        XMVectorSet(1.0f,  0.0f,  0.0f, 0.0f), // +X
        XMVectorSet(-1.0f,  0.0f,  0.0f, 0.0f), // -X
        XMVectorSet(0.0f,  1.0f,  0.0f, 0.0f), // +Y
        XMVectorSet(0.0f, -1.0f,  0.0f, 0.0f), // -Y
        XMVectorSet(0.0f,  0.0f, -1.0f, 0.0f), // +Z
        XMVectorSet(0.0f,  0.0f, 1.0f, 0.0f), // -Z
    };

    // Up vectors for the cubemap faces
    XMVECTOR ups[6] = {
        XMVectorSet(0.0f, 1.0f,  0.0f, 0.0f), // +X
        XMVectorSet(0.0f, 1.0f,  0.0f, 0.0f), // -X
        XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), // +Y
        XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), // -Y
        XMVectorSet(0.0f, 1.0f,  0.0f, 0.0f), // +Z
        XMVectorSet(0.0f, 1.0f,  0.0f, 0.0f), // -Z
    };

    std::array<XMMATRIX, 6> viewMatrices{};
    XMVECTOR lightPos = XMLoadFloat3(&pos);

    for (int i = 0; i < 6; ++i) {
        viewMatrices[i] = XMMatrixLookToRH(
            lightPos,     // Eye position
            targets[i],   // Look direction
            ups[i]        // Up direction
        );
    }

    return viewMatrices;
}

std::wstring GetCacheFilePath(const std::wstring& fileName, const std::wstring& directory) {
    std::filesystem::path workingDir = std::filesystem::current_path();
    std::filesystem::path cacheDir = workingDir / L"cache" / directory;
    std::filesystem::create_directories(cacheDir);
    std::filesystem::path filePath = cacheDir / fileName;
    return filePath.wstring();
}

std::string tolower(const std::string& str) {
	std::string lower = str;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
	return lower;
}

std::vector<std::string> GetFilesInDirectoryMatchingExtension(const std::wstring& directory, const std::wstring& extension) {
    std::vector<std::string> hdrFiles;

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_regular_file() && entry.path().extension() == extension)
            {
                hdrFiles.push_back(entry.path().stem().string());
            }
        }
    }
    catch (const std::exception& e)
    {
        spdlog::error(std::string("Error accessing directory: ") + e.what());
    }

    return hdrFiles;
}

bool OpenFileDialog(std::wstring& selectedFile, const std::wstring& filter) {
    wchar_t fileBuffer[MAX_PATH] = { 0 };

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    //ofn.lpstrFilter = filter.c_str();  // Use the provided filter
    ofn.nFilterIndex = 1;  // Default to the first filter
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;  // Prevent directory change

    // Show the file dialog
    if (GetOpenFileNameW(&ofn) == TRUE) {
        selectedFile = fileBuffer;
        return true;  // File was selected
    }

    return false;  // Dialog was canceled or failed
}

void CopyFileToDirectory(const std::wstring& sourceFile, const std::wstring& destinationDirectory) {
    try
    {
        std::filesystem::path destinationPath = destinationDirectory;
        destinationPath /= std::filesystem::path(sourceFile).filename();

        // Copy the file to the destination
        std::filesystem::copy_file(sourceFile, destinationPath, std::filesystem::copy_options::overwrite_existing);

        std::ofstream fileStream(destinationPath, std::ios::out | std::ios::binary | std::ios::app);
        fileStream.flush();  // Flush the file stream to ensure the data is written
        fileStream.close();

        spdlog::info("File copied to: {}", ws2s(destinationPath.wstring()));
    }
    catch (const std::exception& e)
    {
        spdlog::error(std::string("Error copying file: ") + e.what());
    }
}

std::wstring GetExePath() {
    TCHAR buffer[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
    return std::wstring(buffer).substr(0, pos);
}

std::wstring getFileNameFromPath(const std::wstring& path) {
    size_t lastSlash = path.find_last_of(L"\\/");
    size_t fileNameStart = (lastSlash == std::wstring::npos) ? 0 : lastSlash + 1;

    size_t lastDot = path.find_last_of(L'.');
    if (lastDot == std::wstring::npos || lastDot < fileNameStart) {
        lastDot = path.length();
    }

    return path.substr(fileNameStart, lastDot - fileNameStart);
}

std::array<ClippingPlane, 6> GetFrustumPlanesPerspective(const float aspectRatio, const float fovRad, const float nearClip, const float farClip) {
    std::array<ClippingPlane, 6> planes = {};

    float tanHalfFOV = tan(fovRad / 2.0f);

    // Near and Far Planes (aligned with Z-axis)
    planes[0] = { DirectX::XMFLOAT4(0, 0, -1, -nearClip) }; // Near plane
    planes[1] = { DirectX::XMFLOAT4(0, 0, 1, farClip) };    // Far plane

    planes[2] = { DirectX::XMFLOAT4(1, 0, -tanHalfFOV * aspectRatio, 0) }; // Left plane
    planes[3] = { DirectX::XMFLOAT4( - 1, 0, -tanHalfFOV * aspectRatio, 0) }; // Right plane

    planes[4] = { DirectX::XMFLOAT4(0, 1, -tanHalfFOV, 0) }; // Bottom plane
    planes[5] = { DirectX::XMFLOAT4(0, -1, -tanHalfFOV, 0) }; // Top plane

    // Normalize the planes
    for (int i = 0; i < 6; ++i) {
        float A = planes[i].plane.x;
        float B = planes[i].plane.y;
        float C = planes[i].plane.z;
        float D = planes[i].plane.w;
        float length = sqrt(A * A + B * B + C * C);
        planes[i].plane.x = A / length;
        planes[i].plane.y = B / length;
        planes[i].plane.z = C / length;
        planes[i].plane.w = D / length;
    }

    return planes;
}

std::array<ClippingPlane, 6> GetFrustumPlanesOrthographic(const float left, const float right, const float top, const float bottom, const float nearClip, const float farClip) {
    std::array<ClippingPlane, 6> planes = {};

    // Near and Far Planes (aligned with Z-axis)
    planes[0] = { DirectX::XMFLOAT4(0, 0, -1, -nearClip) }; // Near plane
    planes[1] = { DirectX::XMFLOAT4(0, 0, 1, farClip) };    // Far plane

    planes[2] = { DirectX::XMFLOAT4(1, 0, 0, -left) }; // Left plane
    planes[3] = { DirectX::XMFLOAT4(-1, 0, 0, right) }; // Right plane

    planes[4] = { DirectX::XMFLOAT4(0, 1, 0, -bottom) }; // Bottom plane
    planes[5] = { DirectX::XMFLOAT4(0, -1, 0, top) }; // Top plane

    return planes;
}

DirectX::XMFLOAT3 Subtract(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
	return DirectX::XMFLOAT3(a.x - b.x, a.y - b.y, a.z - b.z);
}

DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
	return DirectX::XMFLOAT3(a.x + b.x, a.y + b.y, a.z + b.z);
}

DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& a, const float scale) {
	return DirectX::XMFLOAT3(a.x * scale, a.y * scale, a.z * scale);
}

XMFLOAT3X3 GetUpperLeft3x3(const XMMATRIX& matrix) {
    XMFLOAT3X3 result;

    // Extract the upper-left 3x3 part of the XMMATRIX
    result.m[0][0] = matrix.r[0].m128_f32[0]; // Row 0, Col 0
    result.m[0][1] = matrix.r[0].m128_f32[1]; // Row 0, Col 1
    result.m[0][2] = matrix.r[0].m128_f32[2]; // Row 0, Col 2

    result.m[1][0] = matrix.r[1].m128_f32[0]; // Row 1, Col 0
    result.m[1][1] = matrix.r[1].m128_f32[1]; // Row 1, Col 1
    result.m[1][2] = matrix.r[1].m128_f32[2]; // Row 1, Col 2

    result.m[2][0] = matrix.r[2].m128_f32[0]; // Row 2, Col 0
    result.m[2][1] = matrix.r[2].m128_f32[1]; // Row 2, Col 1
    result.m[2][2] = matrix.r[2].m128_f32[2]; // Row 2, Col 2

    return result;
}

std::string GetFileExtension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos || dotPos == filePath.length() - 1) {
        return ""; // No extension found or ends with a dot
    }
    return filePath.substr(dotPos + 1);
}

DirectX::XMMATRIX GetProjectionMatrixForLight(LightInfo info) {
    switch (info.type) {
    case Components::LightType::Spot:
        return XMMatrixPerspectiveFovRH(acos(info.outerConeAngle) * 2, 1.0, info.nearPlane, info.farPlane);
        break;
    case Components::LightType::Point:
        return XMMatrixPerspectiveFovRH(XM_PI / 2, 1.0, info.nearPlane, info.farPlane);
        break;
    }
}

DirectX::XMVECTOR QuaternionFromAxisAngle(const XMFLOAT3& dir) {
    XMVECTOR targetDirection = XMVector3Normalize(XMLoadFloat3(&dir));
    float dotProduct = XMVectorGetX(XMVector3Dot(defaultDirection, targetDirection));
	DirectX::XMVECTOR rot;
    if (dotProduct < -0.9999f) {
        XMVECTOR perpendicularAxis = XMVector3Cross(defaultDirection, XMVectorSet(1, 0, 0, 0));
        if (XMVector3Length(perpendicularAxis).m128_f32[0] < 0.01f) {
            perpendicularAxis = XMVector3Cross(defaultDirection, XMVectorSet(0, 1, 0, 0));
        }
        perpendicularAxis = XMVector3Normalize(perpendicularAxis);
        rot = XMQuaternionRotationAxis(perpendicularAxis, XM_PI);
    }
    else if (dotProduct > 0.9999f) {
        rot = XMQuaternionIdentity();
    }
    else {
        XMVECTOR rotationAxis = XMVector3Normalize(XMVector3Cross(defaultDirection, targetDirection));
        float rotationAngle = acosf(dotProduct);
        rot = XMQuaternionRotationAxis(rotationAxis, rotationAngle);
    }
	return rot;
}

XMFLOAT3 GetGlobalPositionFromMatrix(const DirectX::XMMATRIX& mat) {
    XMFLOAT4X4 matFloats;
    XMStoreFloat4x4(&matFloats, mat);
    return XMFLOAT3(matFloats._41, matFloats._42, matFloats._43);
}