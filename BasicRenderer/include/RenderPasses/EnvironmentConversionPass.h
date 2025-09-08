#pragma once

# include <directx/d3d12.h>
#include <filesystem>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/UploadManager.h"
#include "Managers/Singletons/ReadbackManager.h"
#include "Managers/EnvironmentManager.h"

class EnvironmentConversionPass : public RenderPass {
public:
    EnvironmentConversionPass() {
        getSkyboxResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("skyboxResolution");
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        builder->WithShaderResource(Builtin::Environment::WorkingHDRIGroup)
            .WithUnorderedAccess(Builtin::Environment::WorkingCubemapGroup);
    }

    void Setup() override {
		CreateEnvironmentConversionPSO();
    }

	// This pass was broken into multiple passes to avoid device timeout on slower GPUs
    PassReturn Execute(RenderContext& context) override {
        const uint16_t skyboxRes = getSkyboxResolution();

        auto dev = DeviceManager::GetInstance().GetDevice();

		auto& cl = context.commandList;
        cl.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), std::nullopt);

        // Bind layout + pipeline
        cl.BindLayout(m_layout->GetHandle());
        cl.BindPipeline(m_pso->GetHandle());

        EnvironmentManager& manager = *context.environmentManager;
        auto environments = manager.GetAndClearEncironmentsToConvert();

        for (auto& env : environments)
        {
            auto srcTex = env->GetHDRITexture(); // equirectangular HDRI
            auto dstCubemap = env->GetEnvironmentCubemap(); // cube resource (must support UAV)

            const uint32_t srcSrvIndex = srcTex->GetBuffer()->GetSRVInfo(0).slot.index;

            const uint32_t groupSize = 8;
            const uint32_t gx = (skyboxRes + groupSize - 1) / groupSize;
            const uint32_t gy = (skyboxRes + groupSize - 1) / groupSize;

            for (uint32_t face = 0; face < 6; ++face)
            {
                const uint32_t dstFaceUavIndex =
                    dstCubemap->GetBuffer()->GetUAVShaderVisibleInfo(face).slot.index;

                // Root constants payload: [srcSrv, dstFaceUav, face, size]
                uint32_t pc[4] = { srcSrvIndex, dstFaceUavIndex, face, (uint32_t)skyboxRes };

                // Push constants to CS: (set=0, binding=0) matches b0, space0 in HLSL
                cl.PushConstants(rhi::ShaderStage::Compute,
                    /*set*/0, /*binding*/0,
                    /*dstOffset32*/0, /*num32*/4, pc);

                cl.Dispatch(gx, gy, 1);
            }
        }

        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    std::function<uint16_t()> getSkyboxResolution;

    rhi::PipelineLayoutPtr m_layout;
    rhi::PipelinePtr        m_pso;

    void CreateEnvironmentConversionPSO() {
        auto dev = DeviceManager::GetInstance().GetDevice();

        rhi::StaticSamplerDesc s{};
        s.visibility = rhi::ShaderStage::Compute;
        s.set = 0;   // space0
        s.binding = 0;   // s0
        s.arrayCount = 1;
        s.sampler.minFilter = rhi::Filter::Linear;
        s.sampler.magFilter = rhi::Filter::Linear;
        s.sampler.mipFilter = rhi::MipFilter::Linear;
        s.sampler.addressU = rhi::AddressMode::Clamp;
        s.sampler.addressV = rhi::AddressMode::Clamp;
        s.sampler.addressW = rhi::AddressMode::Clamp;

        rhi::PushConstantRangeDesc pc{};
        pc.visibility = rhi::ShaderStage::Compute;
        pc.num32BitValues = 4;    // SrcEnvSrvIndex, DstFaceUavIndex, Face, Size
        pc.set = 0;    // space0
        pc.binding = 0;    // b0

        rhi::PipelineLayoutDesc ld{};
        ld.flags = rhi::PipelineLayoutFlags::PF_None;
        ld.pushConstants = { &pc, 1 };
        ld.staticSamplers = { &s, 1 };
        m_layout = dev.CreatePipelineLayout(ld);
        if (!m_layout || !m_layout->IsValid()) throw std::runtime_error("EnvConvert: layout failed");
        m_layout->SetName("EnvConvert.ComputeLayout");

        ShaderInfoBundle sib;
        sib.computeShader = { L"shaders/envToCubemap.hlsl", L"CSMain", L"cs_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);

        rhi::SubobjLayout soLayout{ m_layout->GetHandle() };
        rhi::SubobjShader soCS{ rhi::ShaderStage::Compute, rhi::DXIL(compiled.computeShader.Get()) };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soCS),
        };
        m_pso = dev.CreatePipeline(items, (uint32_t)std::size(items));
        if (!m_pso || !m_pso->IsValid()) throw std::runtime_error("EnvConvert: PSO failed");
        m_pso->SetName("EnvConvert.ComputePSO");
    }
};
