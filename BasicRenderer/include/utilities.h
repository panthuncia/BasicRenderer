#pragma once

#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <wrl.h>
#include <d3dcompiler.h>

#include "Light.h"
#include "MeshData.h"
#include "Camera.h"

class DescriptorHeap;
class RenderableObject;
class Mesh;

void ThrowIfFailed(HRESULT hr);

template<typename... Args>
void print(Args... args) {
    (std::cout << ... << args) << std::endl;
}

std::shared_ptr<RenderableObject> RenderableFromData(const std::vector<const MeshData*>& meshData, std::wstring name);
std::shared_ptr<Mesh> MeshFromData(const MeshData& meshData, std::wstring name);

XMMATRIX RemoveScalingFromMatrix(XMMATRIX& initialMatrix);
std::shared_ptr<Texture> loadTextureFromFileDXT(std::wstring ddsFilePath, std::shared_ptr<Sampler> sampler = nullptr);
std::shared_ptr<Texture> loadTextureFromFileSTBI(std::string filename, std::shared_ptr<Sampler> sampler = nullptr);
std::shared_ptr<Texture> loadCubemapFromFile(const char* topPath, const char* bottomPath, const char* leftPath, const char* rightPath, const char* frontPath, const char* backPath);
std::shared_ptr<Texture> loadCubemapFromFile(std::wstring ddsFilePath);
template <typename T1, typename T2>
bool mapHasKeyNotAsValue(std::unordered_map<T1, T2>& map, T1 key, T2 val) {
    return map.contains(key) && map[key] != val;
}

template <typename T1, typename T2>
bool mapHasKeyAsValue(std::unordered_map<T1, T2>& map, T1 key, T2 val) {
    return map.contains(key) && map[key] != val;
}

template <typename T1, typename T2>
void CombineMaps(std::unordered_map<T1, T2>& dest, const std::unordered_map<T1, T2>& src) {
	for (const auto& [key, val] : src) {
		dest[key] = val;
	}
}

struct Cascade {
    float size;
    XMMATRIX orthoMatrix;
    XMMATRIX viewMatrix;
	std::array<ClippingPlane, 6> frustumPlanes;
};

DirectX::XMMATRIX createDirectionalLightViewMatrix(XMVECTOR lightDir, XMVECTOR center);

std::vector<Cascade> setupCascades(int numCascades, Light& light, Camera& camera, const std::vector<float>& cascadeSplits);

std::vector<float> calculateCascadeSplits(int numCascades, float zNear, float zFar, float maxDist, float lambda = 0.9f);

std::string ws2s(const std::wstring& wstr);

std::wstring s2ws(const std::string& str);

DXGI_FORMAT DetermineTextureFormat(int channels, bool sRGB, bool isDSV);

std::vector<stbi_uc> ExpandImageData(const stbi_uc* image, int width, int height);

// Helper functions for creating resources

CD3DX12_RESOURCE_DESC CreateTextureResourceDesc(
    DXGI_FORMAT format,
    int width,
    int height,
    int arraySize = 1,
    int mipLevels = 1,
    bool isCubemap = false,
    bool allowRTV = false,
    bool allowDSV = false,
    bool allowUAV = false);

ComPtr<ID3D12Resource> CreateCommittedTextureResource(
    ID3D12Device* device,
    const CD3DX12_RESOURCE_DESC& desc,
    D3D12_CLEAR_VALUE* clearValue = nullptr,
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

ShaderVisibleIndexInfo CreateShaderResourceView(
    ID3D12Device* device,
    ID3D12Resource* resource,
    DXGI_FORMAT format,
    DescriptorHeap* srvHeap,
    int mipLevels = 1,
    bool isCubemap = false,
    bool isArray = false,
    int arraySize = 1);

ShaderVisibleIndexInfo CreateUnorderedAccessView(
    ID3D12Device* device,
    ID3D12Resource* resource,
    DXGI_FORMAT format,
    DescriptorHeap* uavHeap,
    bool isArray,
    int arraySize,
    int mipSlice = 0,
    int firstArraySlice = 0,
    int planeSlice = 0);

NonShaderVisibleIndexInfo CreateNonShaderVisibleUnorderedAccessView(
    ID3D12Device* device,
    ID3D12Resource* resource,
    DXGI_FORMAT format,
    DescriptorHeap* uavHeap,
    bool isArray,
    int arraySize,
    int mipSlice = 0,
    int firstArraySlice = 0,
    int planeSlice = 0);

std::vector<NonShaderVisibleIndexInfo> CreateRenderTargetViews(
    ID3D12Device* device,
    ID3D12Resource* resource,
    DXGI_FORMAT format,
    DescriptorHeap* rtvHeap,
    bool isCubemap,
    bool isArray,
    int arraySize = 1,
    int mipLevels = 1);

std::vector<NonShaderVisibleIndexInfo> CreateDepthStencilViews(
    ID3D12Device* device,
    ID3D12Resource* resource,
    DescriptorHeap* dsvHeap,
    bool isCubemap = false,
    bool isArray = false,
    int arraySize = 1);

void UploadTextureData(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* textureResource,
    const void* data,
    int width,
    int height,
    int channels);

std::array<DirectX::XMMATRIX, 6> GetCubemapViewMatrices(XMFLOAT3 pos);

std::wstring GetCacheFilePath(const std::wstring& fileName, const std::wstring& directory);

inline uint16_t CalculateMipLevels(uint16_t width, uint16_t height) {
    return static_cast<uint16_t>(std::floor(std::log2((std::max)(width, height)))) + 1;
}

std::string tolower(const std::string& str);

std::vector<std::string> GetFilesInDirectoryMatchingExtension(const std::wstring& directory, const std::wstring& extension);

bool OpenFileDialog(std::wstring& selectedFile, const std::wstring& filter);

void CopyFileToDirectory(const std::wstring& sourceFile, const std::wstring& destinationDirectory);

std::wstring GetExePath();

std::wstring getFileNameFromPath(const std::wstring& path);

std::array<ClippingPlane, 6> GetFrustumPlanesPerspective(const float aspectRatio, const float fovRad, const float nearClip, const float farClip);

std::array<ClippingPlane, 6> GetFrustumPlanesOrthographic(const float left, const float right, const float top, const float bottom, const float nearClip, const float farClip);

DirectX::XMFLOAT3 Subtract(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b);

DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b);

DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& a, const float scalar);

XMFLOAT3X3 GetUpperLeft3x3(const XMMATRIX& matrix);

template <class T>
inline void hash_combine(std::size_t & s, const T & v)
{
    std::hash<T> h;
    s^= h(v) + 0x9e3779b9 + (s<< 6) + (s>> 2);
}

std::string GetFileExtension(const std::string& filePath);