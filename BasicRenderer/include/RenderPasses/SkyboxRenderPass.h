#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"

class SkyboxRenderPass : public RenderPass {
public:
    SkyboxRenderPass() {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithShaderResource(Builtin::Environment::CurrentCubemap, Builtin::Environment::InfoBuffer)
            .WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
            .WithRenderTarget(Builtin::Color::HDRColorTarget);
    }

    void Setup() override {
        m_vertexBufferView = CreateSkyboxVertexBuffer();
        CreateSkyboxRootSignature();
        CreateSkyboxPSO();

        m_pHDRTarget = m_resourceRegistryView->Request<PixelBuffer>(Builtin::Color::HDRColorTarget);
        m_pPrimaryDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);

        m_environmentBufferDescriptorIndex = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::Environment::InfoBuffer)->GetSRVInfo(0).slot.index;
    }

    PassReturn Execute(RenderContext& context) override {

        auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

        commandList.SetVertexBuffers(0, 1, &m_vertexBufferView);

        //CD3DX12_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(context.renderResolution.x), static_cast<float>(context.renderResolution.y));
        //CD3DX12_RECT scissorRect(0, 0, context.renderResolution.x, context.renderResolution.y);
        //commandList->RSSetViewports(1, &viewport);
        //commandList->RSSetScissorRects(1, &scissorRect);

        ////CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
        //auto rtvHandle = m_pHDRTarget->GetRTVInfo(0).cpuHandle;
        //auto& dsvHandle = m_pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;

        //// Clear HDR target
        //auto& clearColor = m_pHDRTarget->GetClearColor();
        //commandList->ClearRenderTargetView(rtvHandle, &clearColor[0], 0, nullptr);

        //commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		rhi::PassBeginInfo passInfo{};
		passInfo.width = m_pHDRTarget->GetWidth();
		passInfo.height = m_pHDRTarget->GetHeight();
		rhi::DepthAttachment depthAttachment{};
		depthAttachment.dsv = m_pPrimaryDepthBuffer->GetDSVInfo(0).slot;
		depthAttachment.depthLoad = rhi::LoadOp::Load;
		depthAttachment.depthStore = rhi::StoreOp::DontCare;
		rhi::ColorAttachment colorAttachment{};
		colorAttachment.rtv = m_pHDRTarget->GetRTVInfo(0).slot;
		colorAttachment.loadOp = rhi::LoadOp::Load;
		colorAttachment.storeOp = rhi::StoreOp::Store;
		colorAttachment.clear = m_pHDRTarget->GetClearColor();
		passInfo.colors = { &colorAttachment };
		passInfo.depth = &depthAttachment;
		commandList.BeginPass(passInfo);

		commandList.BindLayout(m_skyboxRootSignature->GetHandle());
		commandList.BindPipeline(m_skyboxPSO->GetHandle());

        auto viewMatrix = context.currentScene->GetPrimaryCamera().get<Components::Camera>().info.view;
        viewMatrix.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); // Skybox has no translation
        auto viewProjectionMatrix = XMMatrixMultiply(viewMatrix, context.currentScene->GetPrimaryCamera().get<Components::Camera>().info.jitteredProjection);
        
		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, 0, 0, 16, &viewProjectionMatrix);
		commandList.PushConstants(rhi::ShaderStage::Pixel, 0, 1, 0, 1, &m_environmentBufferDescriptorIndex);
        commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
        commandList.Draw(36, 1, 0, 0); // Skybox cube

        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    rhi::VertexBufferView m_vertexBufferView;
    std::shared_ptr<Buffer> m_vertexBuffer;
    //std::shared_ptr<Texture> m_texture = nullptr;

    rhi::PipelineLayoutPtr m_skyboxRootSignature;
    rhi::PipelinePtr m_skyboxPSO;

    std::shared_ptr<PixelBuffer> m_pHDRTarget = nullptr;
    std::shared_ptr<PixelBuffer> m_pPrimaryDepthBuffer = nullptr;

    int m_environmentBufferDescriptorIndex = -1;

    struct SkyboxVertex {
        XMFLOAT3 position;
    };

    // Define the vertices for the full-screen triangle
    SkyboxVertex skyboxVertices[36] = {
        {XMFLOAT3{-1.0,  1.0, -1.0}},
        {XMFLOAT3{-1.0, -1.0, -1.0 }},
        {XMFLOAT3{1.0, -1.0, -1.0 }},
        {XMFLOAT3{1.0, -1.0, -1.0 } },
        {XMFLOAT3{1.0,  1.0, -1.0 }},
        {XMFLOAT3{-1.0,  1.0, -1.0 } },

        {XMFLOAT3{-1.0, -1.0,  1.0 }},
        {XMFLOAT3{-1.0, -1.0, -1.0 } },
        {XMFLOAT3{-1.0,  1.0, -1.0 }},
        {XMFLOAT3{-1.0,  1.0, -1.0 } },
        {XMFLOAT3{-1.0,  1.0,  1.0 }},
        {XMFLOAT3{-1.0, -1.0,  1.0 } },

        {XMFLOAT3{1.0, -1.0, -1.0 }},
        {XMFLOAT3{1.0, -1.0,  1.0 } },
        {XMFLOAT3{1.0,  1.0,  1.0 }},
        {XMFLOAT3{1.0,  1.0,  1.0 } },
        {XMFLOAT3{1.0,  1.0, -1.0 }},
        {XMFLOAT3{1.0, -1.0, -1.0 } },

        {XMFLOAT3{-1.0, -1.0,  1.0 }},
        {XMFLOAT3{-1.0,  1.0,  1.0 } },
        {XMFLOAT3{1.0,  1.0,  1.0 }},
        {XMFLOAT3{1.0,  1.0,  1.0 } },
        {XMFLOAT3{1.0, -1.0,  1.0 }},
        {XMFLOAT3{-1.0, -1.0,  1.0 } },

        {XMFLOAT3{-1.0,  1.0, -1.0 }},
        {XMFLOAT3{1.0,  1.0, -1.0 } },
        {XMFLOAT3{1.0,  1.0,  1.0 }},
        {XMFLOAT3{1.0,  1.0,  1.0 } },
        {XMFLOAT3{-1.0,  1.0,  1.0 }},
        {XMFLOAT3{-1.0,  1.0, -1.0 } },

        {XMFLOAT3{-1.0, -1.0, -1.0 }},
        {XMFLOAT3{-1.0, -1.0,  1.0 } },
        {XMFLOAT3{1.0, -1.0, -1.0 }},
        {XMFLOAT3{1.0, -1.0, -1.0 } },
        {XMFLOAT3{-1.0, -1.0,  1.0 }},
        {XMFLOAT3{1.0, -1.0,  1.0 } }

    };

    rhi::VertexBufferView CreateSkyboxVertexBuffer() {

        const UINT vertexBufferSize = static_cast<UINT>(36 * sizeof(SkyboxVertex));

        m_vertexBuffer = Buffer::CreateShared(rhi::HeapType::DeviceLocal, vertexBufferSize);
        BUFFER_UPLOAD(skyboxVertices, vertexBufferSize, m_vertexBuffer, 0);
		m_vertexBuffer->SetName(L"Skybox VB");

        BUFFER_UPLOAD((void*)skyboxVertices, vertexBufferSize, m_vertexBuffer, 0);

		rhi::VertexBufferView vertexBufferView = {};
		vertexBufferView.buffer = m_vertexBuffer->GetAPIResource().GetHandle();
		vertexBufferView.offset = 0;
		vertexBufferView.sizeBytes = vertexBufferSize;
		vertexBufferView.stride = sizeof(SkyboxVertex);

        return vertexBufferView;
    }
    void CreateSkyboxRootSignature() {

        auto dev = DeviceManager::GetInstance().GetDevice();

        rhi::PushConstantRangeDesc pcVS{};
        pcVS.visibility = rhi::ShaderStage::Vertex;
        pcVS.num32BitValues = 16; // view-projection matrix (float4x4)
        pcVS.set = 0; // space0
        pcVS.binding = 0; // b0

        rhi::PushConstantRangeDesc pcPS{};
        pcPS.visibility = rhi::ShaderStage::Pixel;
        pcPS.num32BitValues = 1; // environment SRV index in bindless heap
        pcPS.set = 0; // space0
        pcPS.binding = 1; // b1

        rhi::StaticSamplerDesc s0{}; // point-clamp at s0
        s0.visibility = rhi::ShaderStage::Pixel;
        s0.set = 0;     // space0
        s0.binding = 0;     // s0
        s0.arrayCount = 1;
        s0.sampler.minFilter = rhi::Filter::Nearest;
        s0.sampler.magFilter = rhi::Filter::Nearest;
        s0.sampler.mipFilter = rhi::MipFilter::Nearest;
        s0.sampler.addressU = rhi::AddressMode::Clamp;
        s0.sampler.addressV = rhi::AddressMode::Clamp;
        s0.sampler.addressW = rhi::AddressMode::Clamp;

        rhi::StaticSamplerDesc s1 = s0; // linear-clamp at s1
        s1.binding = 1; // s1
        s1.sampler.minFilter = rhi::Filter::Linear;
        s1.sampler.magFilter = rhi::Filter::Linear;
        s1.sampler.mipFilter = rhi::MipFilter::Linear;

        rhi::PipelineLayoutDesc ld{};
        ld.flags = rhi::PipelineLayoutFlags::PF_AllowInputAssembler;
        const rhi::PushConstantRangeDesc pcs[] = { pcVS, pcPS };
        const rhi::StaticSamplerDesc     sms[] = { s0, s1 };
        ld.pushConstants = { pcs, (uint32_t)std::size(pcs) };
        ld.staticSamplers = { sms, (uint32_t)std::size(sms) };

        auto result = dev.CreatePipelineLayout(ld, m_skyboxRootSignature);
        if (!m_skyboxRootSignature || !m_skyboxRootSignature->IsValid())
            throw std::runtime_error("Skybox: CreatePipelineLayout failed");
        m_skyboxRootSignature->SetName("Skybox.Layout");
    }

    void CreateSkyboxPSO() {
        auto dev = DeviceManager::GetInstance().GetDevice();

        // Compile shaders
        ShaderInfoBundle sib;
        sib.vertexShader = { L"shaders/skybox.hlsl", L"VSMain", L"vs_6_6" };
        sib.pixelShader = { L"shaders/skybox.hlsl", L"PSMain", L"ps_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);

        // Subobjects
        rhi::SubobjLayout  soLayout{ m_skyboxRootSignature->GetHandle() };
        rhi::SubobjShader  soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(compiled.vertexShader.Get()) };
        rhi::SubobjShader  soPS{ rhi::ShaderStage::Pixel,  rhi::DXIL(compiled.pixelShader.Get()) };

        rhi::SubobjRaster  soRaster{};
        soRaster.rs.fill = rhi::FillMode::Solid;
        soRaster.rs.cull = rhi::CullMode::None;
        soRaster.rs.frontCCW = false;
        soRaster.rs.depthBias = D3D12_DEFAULT_DEPTH_BIAS;
        soRaster.rs.depthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        soRaster.rs.slopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;

        rhi::SubobjBlend   soBlend{}; // disable blending
        soBlend.bs.alphaToCoverage = false;
        soBlend.bs.independentBlend = false;
        auto& rt0 = soBlend.bs.attachments[0];
        rt0.enable = false;
        rt0.writeMask = rhi::ColorWriteEnable::All;

        rhi::SubobjDepth  soDepth{};
        soDepth.ds.depthEnable = true;
        soDepth.ds.depthWrite = false;
        soDepth.ds.depthFunc = rhi::CompareOp::Less;

        rhi::SubobjRTVs   soRTVs{};
        soRTVs.rt.count = 1;
        soRTVs.rt.formats[0] = rhi::Format::R16G16B16A16_Float;

        rhi::SubobjDSV    soDSV{};
        soDSV.dsv = rhi::Format::D32_Float;

        rhi::SubobjSample soSample{};
        soSample.sd.count = 1;
        soSample.sd.quality = 0;

        rhi::InputBindingDesc bindings[] = {
            // binding=0, stride=0 means "compute from attributes" during Finalize
            { /*binding*/0, /*stride*/0, rhi::InputRate::PerVertex, /*instanceStepRate*/1 }
        };

        rhi::InputAttributeDesc attrs[] = {
            {
                /*binding*/0,
                /*offset*/rhi::APPEND_ALIGNED, // like D3D12_APPEND_ALIGNED_ELEMENT
                /*format*/rhi::Format::R32G32B32_Float,
                /*semanticName*/"POSITION",
                /*semanticIndex*/0,
                /*location*/0xFFFFFFFFu // Vulkan location auto-assign (ignored by DX12)
              }
        };

        rhi::InputLayoutDesc il{ bindings, 1, attrs, 1 };

        // Resolve offsets (APPEND_ALIGNED) and default strides
        rhi::FinalizedInputLayout fil = Finalize(il);
		rhi::SubobjInputLayout soLayoutIL{ fil };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soVS),
            rhi::Make(soPS),
            rhi::Make(soRaster),
            rhi::Make(soBlend),
            rhi::Make(soDepth),
            rhi::Make(soRTVs),
            rhi::Make(soDSV),
            rhi::Make(soSample),
			rhi::Make(soLayoutIL)
        };

        auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), m_skyboxPSO);
        if (Failed(result)) {
            throw std::runtime_error("Skybox: CreatePipeline failed");
        }
        m_skyboxPSO->SetName("Skybox.GraphicsPSO");
    }
};