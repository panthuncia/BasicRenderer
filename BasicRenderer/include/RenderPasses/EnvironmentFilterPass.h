#pragma once

#include <filesystem>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/UploadManager.h"
#include "Managers/Singletons/ReadbackManager.h"

class EnvironmentFilterPass : public RenderPass {
public:
    EnvironmentFilterPass() = default;

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        // source cubemap as SRV, destination prefiltered cubemap as UAV
        builder->WithShaderResource(Builtin::Environment::WorkingCubemapGroup)
            .WithUnorderedAccess(Builtin::Environment::PrefilteredCubemapsGroup);
    }

    void Setup() override { CreatePrefilterPSO(); }

    PassReturn Execute(RenderContext& context) override {
        auto dev = DeviceManager::GetInstance().GetDevice();

		auto& cl = context.commandList;

        cl.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

        cl.BindLayout(m_layout->GetHandle());
        cl.BindPipeline(m_pso->GetHandle());

        auto environments = context.environmentManager->GetAndClearEnvironmentsToPrefilter();

        for (auto& env : environments)
        {
            const uint32_t baseRes = env->GetReflectionCubemapResolution();
            auto& srcCube = env->GetEnvironmentCubemap();
            auto& dstCubePF = env->GetEnvironmentPrefilteredCubemap();

            const uint32_t srcSrvIndex = srcCube->GetBuffer()->GetSRVInfo(0).slot.index;

            const uint32_t group = 8;

            const uint32_t maxMipLevels = dstCubePF->GetBuffer()->GetNumUAVMipLevels();
            for (uint32_t mip = 0; mip < maxMipLevels; ++mip)
            {
                const uint32_t size = std::max(1u, baseRes >> mip);
                const uint32_t gx = (size + group - 1) / group;
                const uint32_t gy = (size + group - 1) / group;

                float roughness = (maxMipLevels > 1) ? (float)mip / float(maxMipLevels - 1) : 0.0f;

                const uint32_t dstUavIndex =
                    dstCubePF->GetBuffer()->GetUAVShaderVisibleInfo(mip, 0).slot.index;
                for (uint32_t face = 0; face < 6; ++face)
                {
                    // Push constants: [srcSrv, dstUav, face, size, roughnessBits]
                    uint32_t pc[5] = {
                        srcSrvIndex,
                        dstUavIndex,
                        face,
                        size,
                        as_uint(roughness) // pass float as 32-bit payload
                    };

                    cl.PushConstants(rhi::ShaderStage::Compute, /*set*/0, /*binding*/0,
                        /*dstOffset32*/0, /*num32*/5, pc);

                    cl.Dispatch(gx, gy, 1);
                }
            }
        }

        return {};
    }

    void Cleanup(RenderContext&) override {}

private:
    rhi::PipelineLayoutPtr m_layout;
    rhi::PipelinePtr       m_pso;

    void CreatePrefilterPSO() {
        auto dev = DeviceManager::GetInstance().GetDevice();

        // Static sampler s0 (linear clamp)
        rhi::StaticSamplerDesc s{};
        s.visibility = rhi::ShaderStage::Compute;
        s.set = 0;  // space0
        s.binding = 0;  // s0
        s.arrayCount = 1;
        s.sampler.minFilter = rhi::Filter::Linear;
        s.sampler.magFilter = rhi::Filter::Linear;
        s.sampler.mipFilter = rhi::MipFilter::Linear;
        s.sampler.addressU = rhi::AddressMode::Clamp;
        s.sampler.addressV = rhi::AddressMode::Clamp;
        s.sampler.addressW = rhi::AddressMode::Clamp;

        // Push constants: 5x uint32 (last is roughness bits)
        rhi::PushConstantRangeDesc pc{};
        pc.visibility = rhi::ShaderStage::Compute;
        pc.num32BitValues = 5;   // SrcSrv, DstUav, Face, Size, RoughnessBits
        pc.set = 0;   // space0
        pc.binding = 0;   // b0

        rhi::PipelineLayoutDesc ld{};
        ld.flags = rhi::PipelineLayoutFlags::PF_None;
        ld.pushConstants = { &pc, 1 };
        ld.staticSamplers = { &s, 1 };
        m_layout = dev.CreatePipelineLayout(ld);
        if (!m_layout || !m_layout->IsValid()) throw std::runtime_error("EnvFilter: layout failed");
        m_layout->SetName("EnvFilter.ComputeLayout");

        // Compile compute shader
        ShaderInfoBundle sib;
        sib.computeShader = { L"shaders/blurEnvironment.hlsl", L"CSMain", L"cs_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);

        // Create compute PSO
        rhi::SubobjLayout soLayout{ m_layout->GetHandle() };
        rhi::SubobjShader soCS{ rhi::ShaderStage::Compute, rhi::DXIL(compiled.computeShader.Get()) };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soCS),
        };
        auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), m_pso);
        if (Failed(result)) {
            throw std::runtime_error("EnvFilter: PSO failed");
        }
        m_pso->SetName("EnvFilter.ComputePSO");
    }
};