#pragma once

# include <d3d12.h>
#include <filesystem>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "Texture.h"
#include "ResourceHandles.h"
#include "Utilities.h"

class EnvironmentConversionPass : public RenderPass {
public:
    EnvironmentConversionPass(std::shared_ptr<Texture> environmentTexture, std::shared_ptr<Texture> environmentCubeMap, std::shared_ptr<Texture> environmentRadiance, std::string environmentName) {
		m_environmentName = s2ws(environmentName);
        m_texture = environmentTexture;
		m_environmentCubeMap = environmentCubeMap;
		m_environmentRadiance = environmentRadiance;
        m_viewMatrices = GetCubemapViewMatrices({0.0, 0.0, 0.0});
        getSkyboxResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("skyboxResolution");
    }

    void Setup() override {
        auto& manager = DeviceManager::GetInstance();
        auto& device = manager.GetDevice();
        m_vertexBufferView = CreateSkyboxVertexBuffer(device.Get());

        m_sampleDelta = 0.125;
        int totalPhiSamples = static_cast<int>(2.0f * M_PI / m_sampleDelta);
        int totalThetaSamples = static_cast<int>(0.5f * M_PI / m_sampleDelta);
        m_normalizationFactor = M_PI / (totalPhiSamples * totalThetaSamples);

        int maxPhiBatchSize = 50;
        m_numPasses = static_cast<int>(std::ceil(static_cast<float>(totalPhiSamples) / maxPhiBatchSize));
        m_phiBatchSize = totalPhiSamples / m_numPasses;

        auto& queue = manager.GetCommandQueue();
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator)));

        m_commandLists.clear();
        for (int i = 0; i < m_numPasses; i++) {
			ComPtr<ID3D12GraphicsCommandList> commandList;
            ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
            commandList->Close();
			m_commandLists.push_back(commandList);
        }

    }

	// This pass was broken into multiple passes to avoid device timeout on slower GPUs
    std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) override {

        auto& psoManager = PSOManager::getInstance();
        m_pso = psoManager.GetEnvironmentConversionPSO();

		uint16_t skyboxRes = getSkyboxResolution();
        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, skyboxRes, skyboxRes);
        CD3DX12_RECT scissorRect(0, 0, skyboxRes, skyboxRes);

        auto projection = XMMatrixPerspectiveFovRH(XM_PI / 2, 1.0, 0.1, 2.0);

        ThrowIfFailed(m_allocator->Reset());

		std::vector<ID3D12GraphicsCommandList*> commandLists;
        for (int pass = 0; pass < m_numPasses; pass++) {
			auto commandList = m_commandLists[pass].Get();
			commandList->Reset(m_allocator.Get(), m_pso.Get());

            commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissorRect);
            commandList->SetPipelineState(m_pso.Get());
            commandList->SetGraphicsRootSignature(psoManager.GetEnvironmentConversionRootSignature().Get());

            commandList->SetGraphicsRoot32BitConstants(4, 1, &m_normalizationFactor, 0);
            float startPhi = pass * m_phiBatchSize * m_sampleDelta;
            float endPhi = (pass + 1) * m_phiBatchSize * m_sampleDelta;

			commandList->SetGraphicsRoot32BitConstants(2, 1, &startPhi, 0);
			commandList->SetGraphicsRoot32BitConstants(3, 1, &endPhi, 0);

            for (int i = 0; i < 6; i++) {

                CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
                rtvHandles[0] = m_environmentCubeMap->GetHandle().RTVInfo[i].cpuHandle;
                rtvHandles[1] = m_environmentRadiance->GetHandle().RTVInfo[i].cpuHandle;

                CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(context.dsvHeap->GetCPUDescriptorHandleForHeapStart());

                commandList->OMSetRenderTargets(2, rtvHandles, FALSE, nullptr);

                commandList->SetGraphicsRootDescriptorTable(0, m_texture->GetHandle().SRVInfo.gpuHandle);

                auto viewMatrix = m_viewMatrices[i];
                auto viewProjectionMatrix = XMMatrixMultiply(viewMatrix, projection);
                commandList->SetGraphicsRoot32BitConstants(1, 16, &viewProjectionMatrix, 0);
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                commandList->DrawInstanced(36, 1, 0, 0); // Skybox cube
            }
			commandList->Close();
			commandLists.push_back(commandList);
        }
        // We can reuse the results of this pass
		invalidated = false;

		auto path = GetCacheFilePath(m_environmentName + L"_radiance.dds");
		SaveCubemapToDDS(context.device, context.commandList, context.commandQueue, m_environmentRadiance.get(), path);
        path = GetCacheFilePath(m_environmentName + L"_environment.dds");
        SaveCubemapToDDS(context.device, context.commandList, context.commandQueue, m_environmentCubeMap.get(), path);

		return commandLists;
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    BufferHandle vertexBufferHandle;
    ComPtr<ID3D12PipelineState> m_pso;
	std::wstring m_environmentName;
    std::shared_ptr<Texture> m_texture = nullptr;
	std::shared_ptr<Texture> m_environmentCubeMap = nullptr;
	std::shared_ptr<Texture> m_environmentRadiance = nullptr;
    std::array<XMMATRIX, 6> m_viewMatrices;

    std::function<uint16_t()> getSkyboxResolution;

    float m_sampleDelta = 0.0;
	float m_normalizationFactor = 0.0;
	int m_numPasses = 0;
	int m_phiBatchSize = 0;

	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> m_commandLists;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocator;
    struct SkyboxVertex {
        XMFLOAT3 position;
    };

    // Define the vertices for the full-screen triangle
    SkyboxVertex skyboxVertices[36] = {
        XMFLOAT3{-1.0,  1.0, -1.0},
        XMFLOAT3{-1.0, -1.0, -1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{1.0,  1.0, -1.0 },
        XMFLOAT3{-1.0,  1.0, -1.0 },

        XMFLOAT3{-1.0, -1.0,  1.0 },
        XMFLOAT3{-1.0, -1.0, -1.0 },
        XMFLOAT3{-1.0,  1.0, -1.0 },
        XMFLOAT3{-1.0,  1.0, -1.0 },
        XMFLOAT3{-1.0,  1.0,  1.0 },
        XMFLOAT3{-1.0, -1.0,  1.0 },

        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{1.0, -1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0, -1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },

        XMFLOAT3{-1.0, -1.0,  1.0 },
        XMFLOAT3{-1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0, -1.0,  1.0 },
        XMFLOAT3{-1.0, -1.0,  1.0 },

        XMFLOAT3{-1.0,  1.0, -1.0 },
        XMFLOAT3{1.0,  1.0, -1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{-1.0,  1.0,  1.0 },
        XMFLOAT3{-1.0,  1.0, -1.0 },

        XMFLOAT3{-1.0, -1.0, -1.0 },
        XMFLOAT3{-1.0, -1.0,  1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{-1.0, -1.0,  1.0 },
        XMFLOAT3{1.0, -1.0,  1.0 }

    };
    // Create the vertex buffer for the skybox
    D3D12_VERTEX_BUFFER_VIEW CreateSkyboxVertexBuffer(ID3D12Device* device) {
        ComPtr<ID3D12Resource> vertexBuffer;

        const UINT vertexBufferSize = static_cast<UINT>(36 * sizeof(SkyboxVertex));

        // Create a default heap for the vertex buffer
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

        vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, ResourceState::VERTEX, (void*)skyboxVertices);
        ResourceManager::GetInstance().UpdateBuffer(vertexBufferHandle, (void*)skyboxVertices, vertexBufferSize);

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

        vertexBufferView.BufferLocation = vertexBufferHandle.dataBuffer->m_buffer->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(SkyboxVertex);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        return vertexBufferView;
    }
};
