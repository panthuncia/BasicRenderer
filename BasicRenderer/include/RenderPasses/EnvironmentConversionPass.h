#pragma once

# include <directx/d3d12.h>
#include <filesystem>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "Texture.h"
#include "ResourceHandles.h"
#include "Utilities.h"
#include "UploadManager.h"

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

        m_sampleDelta = 0.025;
        int totalPhiSamples = static_cast<int>(2.0f * M_PI / m_sampleDelta);
        int totalThetaSamples = static_cast<int>(0.5f * M_PI / m_sampleDelta);
        m_normalizationFactor = M_PI / (totalPhiSamples * totalThetaSamples);

        int maxPhiBatchSize = 10;
        m_numPasses = static_cast<int>(std::ceil(static_cast<float>(totalPhiSamples) / maxPhiBatchSize));
        m_phiBatchSize = totalPhiSamples / m_numPasses;

        auto& queue = manager.GetCommandQueue();
        for (int i = 0; i < m_numPasses; i++) {
            ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocators[i])));
        }

        m_commandLists.clear();
        for (int i = 0; i < m_numPasses; i++) {
			ComPtr<ID3D12GraphicsCommandList> commandList;
            ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[i].Get(), nullptr, IID_PPV_ARGS(&commandList)));
            commandList->Close();
			m_commandLists.push_back(commandList);
        }
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators.back().Get(), nullptr, IID_PPV_ARGS(&m_copyCommandList)));
		m_copyCommandList->Close();

		CreateEnvironmentConversionRootSignature();
		CreateEnvironmentConversionPSO();
    }

	// This pass was broken into multiple passes to avoid device timeout on slower GPUs
    std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) override {

		uint16_t skyboxRes = getSkyboxResolution();
        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, skyboxRes, skyboxRes);
        CD3DX12_RECT scissorRect(0, 0, skyboxRes, skyboxRes);

        auto projection = XMMatrixPerspectiveFovRH(XM_PI / 2, 1.0, 0.1, 2.0);


		std::vector<ID3D12GraphicsCommandList*> commandLists;
		unsigned int startPass = m_currentPass;
		for (int pass = m_currentPass; pass < m_numPasses && pass < startPass + 1; pass++) { // Do at most one pass per frame to avoid device timeout
            ThrowIfFailed(m_allocators[pass]->Reset());
            auto commandList = m_commandLists[pass].Get();
            commandList->Reset(m_allocators[pass].Get(), environmentConversionPSO.Get());
            m_currentPass += 1;

            if (pass == 0) {
                for (int i = 0; i < 6; i++) {
                    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
					commandList->ClearRenderTargetView(m_environmentRadiance->GetBuffer()->GetRTVInfos()[i].cpuHandle, clearColor, 0, nullptr);
                }
            }
            ID3D12DescriptorHeap* descriptorHeaps[] = {
                ResourceManager::GetInstance().GetSRVDescriptorHeap().Get(), // The texture descriptor heap
                ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get()
            };

            commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

            commandList->SetGraphicsRootSignature(environmentConversionRootSignature.Get());

            commandList->SetGraphicsRootDescriptorTable(0, m_texture->GetBuffer()->GetSRVInfo().gpuHandle);

            commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissorRect);
            commandList->SetPipelineState(environmentConversionPSO.Get());

            commandList->SetGraphicsRoot32BitConstants(4, 1, &m_normalizationFactor, 0);
            float startPhi = pass * m_phiBatchSize * m_sampleDelta;
            float endPhi = (pass + 1) * m_phiBatchSize * m_sampleDelta;

			commandList->SetGraphicsRoot32BitConstants(2, 1, &startPhi, 0);
			commandList->SetGraphicsRoot32BitConstants(3, 1, &endPhi, 0);

            for (int i = 0; i < 6; i++) {

                CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
                rtvHandles[0] = m_environmentCubeMap->GetBuffer()->GetRTVInfos()[i].cpuHandle;
                rtvHandles[1] = m_environmentRadiance->GetBuffer()->GetRTVInfos()[i].cpuHandle;

                CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(context.dsvHeap->GetCPUDescriptorHandleForHeapStart());

                commandList->OMSetRenderTargets(2, rtvHandles, FALSE, nullptr);

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
        if (m_currentPass == m_numPasses) {
            
            invalidated = false;
			m_currentPass = 0;

            m_copyCommandList->Reset(m_allocators.back().Get(), nullptr);
            auto path = GetCacheFilePath(m_environmentName + L"_radiance.dds", L"environments");
            SaveCubemapToDDS(context.device, m_copyCommandList.Get(), context.commandQueue, m_environmentRadiance.get(), path);
            path = GetCacheFilePath(m_environmentName + L"_environment.dds", L"environments");
            SaveCubemapToDDS(context.device, m_copyCommandList.Get(), context.commandQueue, m_environmentCubeMap.get(), path);
            m_copyCommandList->Close();
            commandLists.push_back(m_copyCommandList.Get());
        }

		return commandLists;
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    std::shared_ptr<Buffer> vertexBufferHandle;
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
    int m_currentPass = 0;

	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> m_commandLists;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_copyCommandList;
    std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> m_allocators;

    ComPtr<ID3D12RootSignature> environmentConversionRootSignature;
    ComPtr<ID3D12PipelineState> environmentConversionPSO;

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
		UploadManager::GetInstance().UploadData((void*)skyboxVertices, vertexBufferSize, vertexBufferHandle.get(), 0);

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

        vertexBufferView.BufferLocation = vertexBufferHandle->m_buffer->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(SkyboxVertex);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        return vertexBufferView;
    }

    void CreateEnvironmentConversionRootSignature() {
        CD3DX12_DESCRIPTOR_RANGE1 environmentDescriptorRangeSRV;
        environmentDescriptorRangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 in the shader

        CD3DX12_ROOT_PARAMETER1 environmentRootParameters[5];
        environmentRootParameters[0].InitAsDescriptorTable(1, &environmentDescriptorRangeSRV, D3D12_SHADER_VISIBILITY_PIXEL); // Pixel shader will use the SRV
        environmentRootParameters[1].InitAsConstants(16, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX); // Vertex shader will use the constant buffer (b1)
        environmentRootParameters[2].InitAsConstants(1, 2, 0, D3D12_SHADER_VISIBILITY_PIXEL); // Integration range start
        environmentRootParameters[3].InitAsConstants(1, 3, 0, D3D12_SHADER_VISIBILITY_PIXEL); // Integration range end
        environmentRootParameters[4].InitAsConstants(1, 4, 0, D3D12_SHADER_VISIBILITY_PIXEL); // Normalization factor

        D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.ShaderRegister = 0;  // Corresponds to s0 in the shader
        samplerDesc.RegisterSpace = 0;
        samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(environmentRootParameters), environmentRootParameters, 1, &samplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serializedRootSig;
        ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
            throw std::runtime_error("Failed to serialize root signature");
        }

        auto& device = DeviceManager::GetInstance().GetDevice();
        hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&environmentConversionRootSignature));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create root signature");
        }
    }

    void CreateEnvironmentConversionPSO() {
        // Compile shaders
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        PSOManager::GetInstance().CompileShader(L"shaders/envToCubemap.hlsl", L"VSMain", L"vs_6_6", {}, vertexShader);
        PSOManager::GetInstance().CompileShader(L"shaders/envToCubemap.hlsl", L"PSMain", L"ps_6_6", {}, pixelShader);

        static D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
        inputLayoutDesc.pInputElementDescs = inputElementDescs;
        inputLayoutDesc.NumElements = _countof(inputElementDescs);

        D3D12_RASTERIZER_DESC rasterizerDesc = {};
        rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // No culling for full-screen triangle
        rasterizerDesc.FrontCounterClockwise = FALSE;
        rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterizerDesc.DepthClipEnable = TRUE;
        rasterizerDesc.MultisampleEnable = FALSE;
        rasterizerDesc.AntialiasedLineEnable = FALSE;
        rasterizerDesc.ForcedSampleCount = 0;
        rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_BLEND_DESC blendDesc = {};
        blendDesc.IndependentBlendEnable = TRUE;
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        blendDesc.RenderTarget[1].BlendEnable = TRUE;
        blendDesc.RenderTarget[1].SrcBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[1].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[1].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[1].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[1].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[1].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[1].LogicOpEnable = FALSE;
        blendDesc.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;


        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = inputLayoutDesc;   // No input layout needed for full-screen triangle
        psoDesc.pRootSignature = environmentConversionRootSignature.Get();
        psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        psoDesc.RasterizerState = rasterizerDesc;
        psoDesc.BlendState = blendDesc;
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 2;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
        //psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;
        psoDesc.InputLayout = inputLayoutDesc;

        auto& device = DeviceManager::GetInstance().GetDevice();
        auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&environmentConversionPSO));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create skybox PSO");
        }
    }
};
