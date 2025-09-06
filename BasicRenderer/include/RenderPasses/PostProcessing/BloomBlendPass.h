#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Mesh/Mesh.h"
#include "Scene/Scene.h"
#include "Resources/TextureDescription.h"
#include "Managers/Singletons/UploadManager.h"
#include "../shaders/PerPassRootConstants/bloomBlendRootConstants.h"
#include "Resources/PixelBuffer.h"

class BloomBlendPass : public RenderPass {
public:

    BloomBlendPass() {
        CreatePSO();
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        builder->WithShaderResource(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ 1, 1 }))
            .WithUnorderedAccess(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ 0, 1 }));
    }

    void Setup() override {
        m_pHDRTarget = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PostProcessing::UpscaledHDR);
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		rhi::PassBeginInfo passInfo{};
		passInfo.height = m_pHDRTarget->GetHeight();
		passInfo.width = m_pHDRTarget->GetWidth();
		commandList.BeginPass(passInfo);

        commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleStrip);

		commandList.BindLayout(psoManager.GetRootSignature().GetHandle());
		commandList.BindPipeline(m_pso->GetHandle());

        BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

        unsigned int misc[NumMiscUintRootConstants] = {};
		misc[HDR_TARGET_UAV_DESCRIPTOR_INDEX] = m_pHDRTarget->GetUAVShaderVisibleInfo(0).index; // HDR target index
		misc[BLOOM_SOURCE_SRV_DESCRIPTOR_INDEX] = m_pHDRTarget->GetSRVInfo(1).index; // Bloom texture index
        misc[DST_WIDTH] = m_pHDRTarget->GetWidth();
        misc[DST_HEIGHT] = m_pHDRTarget->GetHeight();
		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);

        float miscFloats[NumMiscFloatRootConstants] = {};

        miscFloats[FloatRootConstant0] = 0.001f; // Kernel size
        miscFloats[FloatRootConstant1] = misc[UintRootConstant2] / (float) misc[UintRootConstant3]; // Aspect ratio

		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscFloatRootSignatureIndex, 0, NumMiscFloatRootConstants, &miscFloats);

        commandList.Draw(3, 1, 0, 0); // Fullscreen triangle
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup the render pass
    }

private:

    unsigned int m_mipIndex;
    bool m_isUpsample = false;

    rhi::PipelinePtr m_pso;

	std::shared_ptr<PixelBuffer> m_pHDRTarget;

	PipelineResources m_resourceDescriptorBindings;

    void CreatePSO() {
        auto dev = DeviceManager::GetInstance().GetDevice();

        // 1) Compile shaders (same as before)
        ShaderInfoBundle sib;
        sib.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSNoViewRayMain", L"vs_6_6" };
        sib.pixelShader = { L"shaders/PostProcessing/bloomBlend.hlsl", L"blend", L"ps_6_6" };

        auto compiled = PSOManager::GetInstance().CompileShaders(sib);
        m_resourceDescriptorBindings = compiled.resourceDescriptorSlots;

        auto& layout = PSOManager::GetInstance().GetRootSignature(); // rhi::PipelineLayout&
        rhi::SubobjLayout soLayout{ layout.GetHandle() };
        rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(compiled.vertexShader.Get()) };
        rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel,  rhi::DXIL(compiled.pixelShader.Get()) };

        rhi::RasterState rs{};
        rs.fill = rhi::FillMode::Solid;
        rs.cull = rhi::CullMode::None; // fullscreen triangle: no culling
        rs.frontCCW = false;
        rhi::SubobjRaster soRaster{ rs };

        rhi::BlendState bs{};
        bs.alphaToCoverage = false;
        bs.independentBlend = false;
        bs.numAttachments = 0;
        rhi::SubobjBlend soBlend{ bs };

        rhi::DepthStencilState ds{};
        ds.depthEnable = false;
        ds.depthWrite = false;
        rhi::SubobjDepth soDepth{ ds };

        rhi::RenderTargets rts{};
        rts.count = 0;
        rhi::SubobjRTVs soRTVs{ rts };

        rhi::SubobjDSV soDSV{ rhi::Format::Unknown }; // no DSV
        rhi::SubobjSample soSample{ rhi::SampleDesc{1, 0} };

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
        };

        // 3) Create PSO
        m_pso = dev.CreatePipeline(items, (uint32_t)std::size(items));
        if (!m_pso || !m_pso->IsValid()) {
            throw std::runtime_error("Failed to create upsample PSO (RHI)");
        }
        m_pso->SetName("BloomBlend (RHI)");
    }
};