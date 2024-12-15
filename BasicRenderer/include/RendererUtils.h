#pragma once

#include <string>
#include <d3d12.h>
#include <functional>
#include <wrl/client.h>
#include "ReadbackRequest.h"

class Texture;


class RendererUtils {
public:
    RendererUtils(std::function<void(ReadbackRequest&&)> submitReadbackRequestFunc, Microsoft::WRL::ComPtr<ID3D12Fence> readbackFence)
        : m_submitReadbackRequest(submitReadbackRequestFunc) {
		m_readbackFence = readbackFence;
    }

    void SaveCubemapToDDS(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, Texture* cubemap, const std::wstring& outputFile, UINT64 fenceValue);
    void SaveTextureToDDS(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12CommandQueue* commandQueue, Texture* texture, const std::wstring& outputFile, UINT64 fenceValue);
	ID3D12Fence* GetReadbackFence() { return m_readbackFence.Get(); }
private:
    std::function<void(ReadbackRequest&&)> m_submitReadbackRequest;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_readbackFence;
};