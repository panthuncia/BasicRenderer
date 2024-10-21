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

#include "MeshUtilities.h"
#include "PSOFlags.h"
#include "DirectX/d3dx12.h"
#include "DefaultDirection.h"
#include "Sampler.h"
#include "DescriptorHeap.h"
#include "ReadbackRequest.h"
#include "Material.h"
#include "SettingsManager.h"

void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        // Print the error code for debugging purposes
        std::cerr << "HRESULT failed with error code: " << std::hex << hr << std::endl;
        throw std::runtime_error("HRESULT failed");
    }
}

std::shared_ptr<RenderableObject> RenderableFromData(MeshData meshData, std::wstring name) {
    std::vector<std::shared_ptr<Mesh>> meshes;

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

        std::shared_ptr<Mesh> mesh = Mesh::CreateShared(vertices, geom.indices, geom.material, hasJoints);
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

std::shared_ptr<Texture> loadTextureFromFile(std::string filename) {
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

    TextureDescription desc;
	desc.channels = img.channels;
	desc.width = img.width;
	desc.height = img.height;
	desc.format = format;
	auto buffer = PixelBuffer::Create(desc, { img.data });

    auto sampler = Sampler::GetDefaultSampler();
    return std::make_shared<Texture>(buffer, sampler);
}

std::shared_ptr<Texture> loadCubemapFromFile(const char* topPath, const char* bottomPath, const char* leftPath, const char* rightPath, const char* frontPath, const char* backPath) {
    ImageData top = loadImage(topPath);
	ImageData bottom = loadImage(bottomPath);
	ImageData left = loadImage(leftPath);
	ImageData right = loadImage(rightPath);
	ImageData front = loadImage(frontPath);
	ImageData back = loadImage(backPath);


	TextureDescription desc;
	desc.height = top.height;
	desc.width = top.width;
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
    std::vector<const stbi_uc*> faces = {};
    for (size_t face = 0; face < 6; ++face) {
        for (size_t mip = 0; mip < metadata.mipLevels; ++mip) {
            const DirectX::Image* img = image.GetImage(mip, face, 0); // mip 0, face i, slice 0
            faces.push_back(img->pixels);
        }
    }

    TextureDescription desc;
	desc.channels = 4;
	desc.width = metadata.width;
	desc.height = metadata.height;
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

        // TODO: Figure out why the cascades are kinda broken. Hack by making them thicc for now.
        XMMATRIX orthoMatrix = XMMatrixOrthographicOffCenterRH(minX, maxX, minY, maxY, minZ - 100.0f, maxZ + 100.0f);

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
    bool allowDSV) {

    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
        format,
        width,
        height,
        isCubemap ? 6 * arraySize : arraySize,
        mipLevels);

    if (allowRTV) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (allowDSV) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

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
    srvInfo.cpuHandle = cpuHandle;
    srvInfo.gpuHandle = gpuHandle;

    return srvInfo;
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

void AsyncSaveToDDS(DirectX::ScratchImage scratchImage, const std::wstring& outputFile) {
    HRESULT hr = DirectX::SaveToDDSFile(scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(),
        DirectX::DDS_FLAGS_NONE, outputFile.c_str());
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to save the cubemap to a .dds file!");
    }
}


void SaveCubemapToDDS(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12CommandQueue* commandQueue, Texture* cubemap, const std::wstring& outputFile) {
    D3D12_RESOURCE_DESC resourceDesc = cubemap->GetHandle().texture->GetDesc();

    // Get the number of mip levels and subresources
    UINT numMipLevels = resourceDesc.MipLevels;
    UINT numSubresources = 6 * numMipLevels;  // 6 faces, each with multiple mip levels

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizesInBytes(numSubresources);
    UINT64 totalSize = 0;

    // Get the copyable footprints for each mip level of each face
    device->GetCopyableFootprints(
        &resourceDesc,
        0,
        numSubresources,
        0,
        layouts.data(),
        numRows.data(),
        rowSizesInBytes.data(),
        &totalSize);

    // Describe and create the readback buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = totalSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;  // Buffers don't have a format
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // Buffers should be row-major
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create readback buffer!");
    }
    readbackBuffer->SetName(L"Readback");

    auto initialState = ResourceStateToD3D12(cubemap->GetState());
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(cubemap->GetHandle().texture.Get(), initialState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(1, &barrier);

    // Issue copy commands for each mip level of each face
    for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
        for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex) {
            UINT subresourceIndex = D3D12CalcSubresource(mipLevel, faceIndex, 0, numMipLevels, 6);

            D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
            srcLocation.pResource = cubemap->GetHandle().texture.Get();
            srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            srcLocation.SubresourceIndex = subresourceIndex;

            D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
            dstLocation.pResource = readbackBuffer.Get();
            dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dstLocation.PlacedFootprint = layouts[subresourceIndex];

            commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
        }
    }

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(cubemap->GetHandle().texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, initialState);
    commandList->ResourceBarrier(1, &barrier);

    ReadbackRequest readbackRequest;
    readbackRequest.readbackBuffer = readbackBuffer;
    readbackRequest.layouts = layouts;
    readbackRequest.totalSize = totalSize;
    readbackRequest.outputFile = outputFile;
    readbackRequest.callback = [=]() {
        std::thread([=] {
            void* mappedData = nullptr;
            D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(totalSize) };
            auto hr = readbackBuffer->Map(0, &readRange, &mappedData);
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to map the readback buffer!");
            }

            // Create a ScratchImage and fill it with the cubemap and mipmap data
            DirectX::ScratchImage scratchImage;
            scratchImage.InitializeCube(resourceDesc.Format, resourceDesc.Width, resourceDesc.Height, 1, numMipLevels);  // Initialize with mip levels

            for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
                for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex) {
                    UINT subresourceIndex = D3D12CalcSubresource(mipLevel, faceIndex, 0, numMipLevels, 6);

                    DirectX::Image image;
                    image.width = resourceDesc.Width >> mipLevel;
                    image.height = resourceDesc.Height >> mipLevel;
                    image.format = resourceDesc.Format;
                    image.rowPitch = static_cast<size_t>(layouts[subresourceIndex].Footprint.RowPitch);
                    image.slicePitch = image.rowPitch * image.height;
                    image.pixels = static_cast<uint8_t*>(mappedData) + layouts[subresourceIndex].Offset;

                    const DirectX::Image* destImage = scratchImage.GetImage(mipLevel, faceIndex, 0);
                    
					// Sometimes, due to formatting weirdness, the row pitch of the source image is less than the row pitch of the destination image
					// We copy row by row to avoid heap corruption
                    // I don't know why ScratchImage does this
					// TODO: Does this break smaller mip levels?

                    size_t destRowPitch = destImage->rowPitch;
                    size_t srcRowPitch = image.rowPitch;
                    size_t rowCount = image.height;

                    uint8_t* destPixels = destImage->pixels;
                    uint8_t* srcPixels = image.pixels;

                    for (size_t row = 0; row < rowCount; ++row)
                    {
                        memcpy(destPixels + row * destRowPitch, srcPixels + row * srcRowPitch, (std::min)(destRowPitch, srcRowPitch));
                    }

                    //memcpy(destImage->pixels, image.pixels, image.slicePitch);
                }
            }

            readbackBuffer->Unmap(0, nullptr);

            hr = DirectX::SaveToDDSFile(scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outputFile.c_str());
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to save the cubemap to a .dds file!");
            }
            // Save asynchronously to a DDS file with mipmaps
            //std::async(std::launch::async, AsyncSaveToDDS, std::move(scratchImage), outputFile);
            }).detach();
        };

    std::function<void(ReadbackRequest&&)> submitReadbackRequest = SettingsManager::GetInstance().getSettingGetter<std::function<void(ReadbackRequest&&)>>("readbackRequestHandler")();
    submitReadbackRequest(std::move(readbackRequest));
}

void SaveTextureToDDS(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12CommandQueue* commandQueue, Texture* texture, const std::wstring& outputFile) {
    D3D12_RESOURCE_DESC resourceDesc = texture->GetHandle().texture->GetDesc();

    // Get the number of mip levels and subresources
    UINT numMipLevels = resourceDesc.MipLevels;
    UINT numSubresources = numMipLevels;

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizesInBytes(numSubresources);
    UINT64 totalSize = 0;

    // Get the copyable footprints for each mip level of each face
    device->GetCopyableFootprints(
        &resourceDesc,
        0,
        numSubresources,
        0,
        layouts.data(),
        numRows.data(),
        rowSizesInBytes.data(),
        &totalSize);

    // Describe and create the readback buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = totalSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;  // Buffers don't have a format
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // Buffers should be row-major
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create readback buffer!");
    }
    readbackBuffer->SetName(L"Readback");

    auto initialState = ResourceStateToD3D12(texture->GetState());
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture->GetHandle().texture.Get(), initialState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(1, &barrier);

    // Issue copy commands for each mip level of each face
    for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
        UINT subresourceIndex = D3D12CalcSubresource(mipLevel, 0, 0, numMipLevels, 1);

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = texture->GetHandle().texture.Get();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = subresourceIndex;

        D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
        dstLocation.pResource = readbackBuffer.Get();
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLocation.PlacedFootprint = layouts[subresourceIndex];

        commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

    }

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture->GetHandle().texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, initialState);
    commandList->ResourceBarrier(1, &barrier);

    ReadbackRequest readbackRequest;
    readbackRequest.readbackBuffer = readbackBuffer;
    readbackRequest.layouts = layouts;
    readbackRequest.totalSize = totalSize;
    readbackRequest.outputFile = outputFile;
    readbackRequest.callback = [=]() {
        std::thread([=] {
            void* mappedData = nullptr;
            D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(totalSize) };
            auto hr = readbackBuffer->Map(0, &readRange, &mappedData);
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to map the readback buffer!");
            }

            // Create a ScratchImage and fill it with the cubemap and mipmap data
            DirectX::ScratchImage scratchImage;
			scratchImage.Initialize2D(resourceDesc.Format, resourceDesc.Width, resourceDesc.Height, 1, numMipLevels);

            for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
                UINT subresourceIndex = D3D12CalcSubresource(mipLevel, 0, 0, numMipLevels, 6);

                DirectX::Image image;
                image.width = resourceDesc.Width >> mipLevel;
                image.height = resourceDesc.Height >> mipLevel;
                image.format = resourceDesc.Format;
                image.rowPitch = static_cast<size_t>(layouts[subresourceIndex].Footprint.RowPitch);
                image.slicePitch = image.rowPitch * image.height;
                image.pixels = static_cast<uint8_t*>(mappedData) + layouts[subresourceIndex].Offset;

                const DirectX::Image* destImage = scratchImage.GetImage(mipLevel, 0, 0);

                // Sometimes, due to formatting weirdness, the row pitch of the source image is less than the row pitch of the destination image
                // We copy row by row to avoid heap corruption
                // I don't know why ScratchImage does this
                // TODO: Does this break smaller mip levels?

                size_t destRowPitch = destImage->rowPitch;
                size_t srcRowPitch = image.rowPitch;
                size_t rowCount = image.height;

                uint8_t* destPixels = destImage->pixels;
                uint8_t* srcPixels = image.pixels;

                for (size_t row = 0; row < rowCount; ++row)
                {
                    memcpy(destPixels + row * destRowPitch, srcPixels + row * srcRowPitch, (std::min)(destRowPitch, srcRowPitch));
                }

                //memcpy(destImage->pixels, image.pixels, image.slicePitch);
             
            }

            readbackBuffer->Unmap(0, nullptr);

            hr = DirectX::SaveToDDSFile(scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outputFile.c_str());
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to save the cubemap to a .dds file!");
            }
            // Save asynchronously to a DDS file with mipmaps
            //std::async(std::launch::async, AsyncSaveToDDS, std::move(scratchImage), outputFile);
            }).detach();
        };

    std::function<void(ReadbackRequest&&)> submitReadbackRequest = SettingsManager::GetInstance().getSettingGetter<std::function<void(ReadbackRequest&&)>>("readbackRequestHandler")();
    submitReadbackRequest(std::move(readbackRequest));
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
    ofn.lpstrFilter = filter.c_str();  // Use the provided filter
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