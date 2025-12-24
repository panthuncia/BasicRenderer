#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Scene/Scene.h"
#include "../shaders/PerPassRootConstants/bloomSampleRootConstants.h"

struct BloomSamplePassInputs {
    unsigned int mipIndex;
    bool isUpsample;
    friend bool operator==(const BloomSamplePassInputs&, const BloomSamplePassInputs&) = default;
};

inline rg::Hash64 HashValue(const BloomSamplePassInputs& i) {
    std::size_t seed = 0;

    boost::hash_combine(seed, i.mipIndex);
    boost::hash_combine(seed, i.isUpsample);
    return seed;
}

class BloomSamplePass : public RenderPass {
public:
    // mipIndex selects which mip is used as render target, and which is used as shader resource.
    // E.g. DownsamplePassIndex 0 will downsample from mip 0 to mip 1, and use mip 1 as the render target.
    // If isUpsample is true, it will upsample from mip 1 to mip 0.
    BloomSamplePass() {
        CreatePSO();
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
		auto inputs = Inputs<BloomSamplePassInputs>();
		m_mipIndex = inputs.mipIndex;
		m_isUpsample = inputs.isUpsample;

        if (!m_isUpsample) {
            builder->WithShaderResource(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ m_mipIndex, 1 }))
                .WithRenderTarget(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ m_mipIndex + 1, 1 }));
        }
        else {
            builder->WithShaderResource(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ m_mipIndex + 1, 1 }))
                .WithRenderTarget(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ m_mipIndex, 1 }));
        }
    }

    void Setup() override {
        m_pHDRTarget = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PostProcessing::UpscaledHDR);

        RegisterSRV(Builtin::PostProcessing::UpscaledHDR, m_mipIndex + (m_isUpsample ? 1 : 0));
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

        unsigned int mipOffset = m_isUpsample ? 0 : 1;

		rhi::PassBeginInfo passInfo{};
		rhi::ColorAttachment colorAttachment{};
		colorAttachment.rtv = m_pHDRTarget->GetRTVInfo(m_mipIndex + mipOffset).slot;
		colorAttachment.loadOp = rhi::LoadOp::Load;
		colorAttachment.storeOp = rhi::StoreOp::Store;
		passInfo.colors = { &colorAttachment };
        passInfo.width = m_pHDRTarget->GetWidth() >> (m_mipIndex + mipOffset);
        passInfo.height = m_pHDRTarget->GetHeight() >> (m_mipIndex + mipOffset);
		commandList.BeginPass(passInfo);

        commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleStrip);

        if (m_isUpsample) {
			commandList.BindPipeline(m_upsamplePso->GetHandle());
        }
        else {
			commandList.BindPipeline(m_downsamplePso->GetHandle());
        }

        auto rootSignature = psoManager.GetRootSignature();
		commandList.BindLayout(rootSignature.GetHandle());

		BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

        unsigned int misc[NumMiscUintRootConstants] = {};
        // misc[UintRootConstant0] = m_pHDRTarget->GetSRVInfo(m_mipIndex + (m_isUpsample ? 1 : 0)).index;
        misc[MIP_INDEX] = m_mipIndex;
        misc[MIP_WIDTH] = m_pHDRTarget->GetWidth() >> m_mipIndex;
        misc[MIP_HEIGHT] = m_pHDRTarget->GetHeight() >> m_mipIndex;
		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);

        float miscFloats[NumMiscFloatRootConstants] = {};
        if (m_isUpsample) {
            miscFloats[FILTER_RADIUS] = 0.001f; // Kernel size
            miscFloats[ASPECT_RATIO] = misc[MIP_WIDTH] / (float)misc[MIP_HEIGHT]; // Aspect ratio
        }
        else {
            miscFloats[SRC_TEXEL_SIZE_X] = 1.0f / misc[MIP_WIDTH]; // Texel size X
            miscFloats[SRC_TEXEL_SIZE_Y] = 1.0f / misc[MIP_HEIGHT]; // Texel size Y
        }
        //commandList->SetGraphicsRoot32BitConstants(MiscFloatRootSignatureIndex, NumMiscFloatRootConstants, &miscFloats, 0);
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

    rhi::PipelinePtr m_downsamplePso;
    rhi::PipelinePtr m_upsamplePso;

    std::shared_ptr<PixelBuffer> m_pHDRTarget;

	PipelineResources m_resourceDescriptorBindings;

    void CreatePSO() {
        auto dev = DeviceManager::GetInstance().GetDevice();

        ShaderInfoBundle sib;
        sib.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSNoViewRayMain", L"vs_6_6" };
        sib.pixelShader = { L"shaders/PostProcessing/bloomDownsample.hlsl", L"downsample", L"ps_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);

        m_resourceDescriptorBindings = compiled.resourceDescriptorSlots;

        auto& layout = PSOManager::GetInstance().GetRootSignature(); // rhi::PipelineLayout&
        rhi::SubobjLayout soLayout{ layout.GetHandle() };
        rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(compiled.vertexShader.Get()) };

        rhi::RasterState rs{};
        rs.fill = rhi::FillMode::Solid;
        rs.cull = rhi::CullMode::None;    // fullscreen triangle
        rs.frontCCW = false;
        rhi::SubobjRaster soRaster{ rs };

        rhi::DepthStencilState ds{};
        ds.depthEnable = false;
        ds.depthWrite = false;
        rhi::SubobjDepth soDepth{ ds };

        rhi::RenderTargets rts{};
        rts.count = 1;
        rts.formats[0] = rhi::Format::R16G16B16A16_Float; // RTVFormats[0]
        rhi::SubobjRTVs soRTVs{ rts };

        rhi::SubobjDSV soDSV{ rhi::Format::D32_Float };   // matches DX psoDesc.DSVFormat
        rhi::SubobjSample soSample{ rhi::SampleDesc{1, 0} };

        rhi::SubobjShader soPS_down{ rhi::ShaderStage::Pixel, rhi::DXIL(compiled.pixelShader.Get()) };

        rhi::BlendState bsDown{};
        bsDown.alphaToCoverage = false;
        bsDown.independentBlend = false;
        bsDown.numAttachments = 1;
        bsDown.attachments[0].enable = false; // disabled
        rhi::SubobjBlend soBlendDown{ bsDown };

        {
            const rhi::PipelineStreamItem items[] = {
                rhi::Make(soLayout),
                rhi::Make(soVS),
                rhi::Make(soPS_down),
                rhi::Make(soRaster),
                rhi::Make(soBlendDown),
                rhi::Make(soDepth),
                rhi::Make(soRTVs),
                rhi::Make(soDSV),
                rhi::Make(soSample),
            };

            auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), m_downsamplePso);
            if (Failed(result)) {
                throw std::runtime_error("Failed to create bloom downsample PSO (RHI)");
            }
            m_downsamplePso->SetName("Bloom.Downsample");
        }


        sib.pixelShader = { L"shaders/PostProcessing/bloomUpsample.hlsl", L"upsample", L"ps_6_6" };
        auto compiledUp = PSOManager::GetInstance().CompileShaders(sib);
        rhi::SubobjShader soPS_up{ rhi::ShaderStage::Pixel, rhi::DXIL(compiledUp.pixelShader.Get()) };

        rhi::BlendState bsUp{};
        bsUp.alphaToCoverage = false;
        bsUp.independentBlend = false;
        bsUp.numAttachments = 1;
        {
            auto& a0 = bsUp.attachments[0];
            a0.enable = true;
            a0.srcColor = rhi::BlendFactor::One;
            a0.dstColor = rhi::BlendFactor::One;
            a0.colorOp = rhi::BlendOp::Add;

            a0.srcAlpha = rhi::BlendFactor::One;
            a0.dstAlpha = rhi::BlendFactor::Zero;
            a0.alphaOp = rhi::BlendOp::Add;

            a0.writeMask = rhi::ColorWriteEnable::All;
        }
        rhi::SubobjBlend soBlendUp{ bsUp };

        {
            const rhi::PipelineStreamItem items[] = {
                rhi::Make(soLayout),
                rhi::Make(soVS),
                rhi::Make(soPS_up),
                rhi::Make(soRaster),
                rhi::Make(soBlendUp),
                rhi::Make(soDepth),
                rhi::Make(soRTVs),
                rhi::Make(soDSV),
                rhi::Make(soSample),
            };

            auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), m_upsamplePso);
            if (Failed(result)) {
                throw std::runtime_error("Failed to create bloom upsample PSO (RHI)");
            }
            m_upsamplePso->SetName("Bloom.Upsample");
        }
    }
};