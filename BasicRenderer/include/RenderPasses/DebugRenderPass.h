#pragma once

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "Texture.h"
#include "ResourceHandles.h"

class DebugRenderPass : public RenderPass {
public:
    DebugRenderPass() {}

    void Setup() override {
        auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();
		m_vertexBufferView = CreateFullscreenTriangleVertexBuffer(device.Get());
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator)));
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    }

    std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) override {
        if (m_texture == nullptr) {
            return { };
        }
        auto& psoManager = PSOManager::getInstance();
        m_pso = psoManager.GetDebugPSO();
        auto& commandList = m_commandList;
        ThrowIfFailed(m_allocator->Reset());
		commandList->Reset(m_allocator.Get(), nullptr);

        commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, context.xRes, context.yRes);
        CD3DX12_RECT scissorRect(0, 0, context.xRes, context.yRes);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        commandList->SetPipelineState(m_pso.Get());
        commandList->SetGraphicsRootSignature(psoManager.GetDebugRootSignature().Get());

        commandList->SetGraphicsRootDescriptorTable(0, m_texture->GetHandle().SRVInfo.gpuHandle);
        auto viewMatrix = XMMatrixTranspose(XMMatrixMultiply(XMMatrixScaling(0.2f, 0.2f, 1.0f), XMMatrixTranslation(0.7, -0.7, 0)));
        commandList->SetGraphicsRoot32BitConstants(1, 16, &viewMatrix, 0);

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        commandList->DrawInstanced(4, 1, 0, 0); // Fullscreen quad

		commandList->Close();

		return { commandList.Get() };
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

    void SetTexture(Texture* texture) {
		m_texture = texture;
    }

private:
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    BufferHandle vertexBufferHandle;
    ComPtr<ID3D12PipelineState> m_pso;
    Texture* m_texture = nullptr;

	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12CommandAllocator> m_allocator;

    struct DebugVertex {
        XMFLOAT3 position;
        XMFLOAT2 texcoord;
    };

    // Define the vertices for the full-screen triangle
    DebugVertex fullscreenTriangleVertices[4] = {
        { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT2(0.0, 0.0)},
        { XMFLOAT3(1.0f,  1.0f, 0.0f), XMFLOAT2(1.0, 0.0) },
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0, 1.0) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0, 1.0) }

    };

    // Create the vertex buffer for the full-screen triangle
    D3D12_VERTEX_BUFFER_VIEW CreateFullscreenTriangleVertexBuffer(ID3D12Device* device) {
        ComPtr<ID3D12Resource> vertexBuffer;

        const UINT vertexBufferSize = static_cast<UINT>(4 * sizeof(DebugVertex));

        // Create a default heap for the vertex buffer
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

        vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, ResourceState::VERTEX, (void*)fullscreenTriangleVertices);
		ResourceManager::GetInstance().UpdateBuffer(vertexBufferHandle, (void*)fullscreenTriangleVertices, vertexBufferSize);

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

        vertexBufferView.BufferLocation = vertexBufferHandle.dataBuffer->m_buffer->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(DebugVertex);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        return vertexBufferView;
    }
};
