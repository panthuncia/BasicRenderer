#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Managers/EnvironmentManager.h"
#include "Interfaces/IDynamicDeclaredResources.h"

#include <vector>

class EnvironmentConversionPass : public ComputePass, public IDynamicDeclaredResources {
public:
    EnvironmentConversionPass() {
        getSkyboxResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("skyboxResolution");
        CreateEnvironmentConversionPSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) override {
        for (const auto& j : m_pending) {
            if (!j.srcTexture || !j.dstCubemap) continue;

            builder->WithShaderResource(j.srcTexture);
            builder->WithUnorderedAccess(j.dstCubemap);
        }

        m_declaredResourcesChanged = false;
    }

    void Setup() override {
    }

    void Update(const UpdateExecutionContext& context) override {
        std::vector<Job> newPending;
        auto* updateData = context.hostData ? const_cast<UpdateContext*>(context.hostData->Get<UpdateContext>()) : nullptr;

        if (updateData && updateData->environmentManager) {
            auto environments = updateData->environmentManager->GetAndClearEnvironmentsToConvert();
            newPending.reserve(environments.size());

            for (auto* env : environments) {
                if (!env) continue;

                auto srcTex = env->GetHDRITexture();
                auto dstCubemap = env->GetEnvironmentCubemap();
                if (!srcTex || !dstCubemap) continue;

                auto srcImage = srcTex->ImagePtr();
                auto dstImage = dstCubemap->ImagePtr();
                if (!srcImage || !dstImage) continue;

                newPending.push_back(Job{ srcImage, dstImage });
            }
        }

        auto sameJobs = [](const std::vector<Job>& a, const std::vector<Job>& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (a[i].srcTexture.get() != b[i].srcTexture.get()) return false;
                if (a[i].dstCubemap.get() != b[i].dstCubemap.get()) return false;
            }
            return true;
        };

        if (!sameJobs(m_pending, newPending)) {
            m_declaredResourcesChanged = true;
            m_pending = std::move(newPending);
        }
    }

    PassReturn Execute(PassExecutionContext& executionContext) override {
        auto* renderContext = executionContext.hostData ? const_cast<RenderContext*>(executionContext.hostData->Get<RenderContext>()) : nullptr;
        if (!renderContext) return {};
        auto& context = *renderContext;
        if (m_pending.empty()) return {};

        const uint16_t skyboxRes = getSkyboxResolution();

        auto dev = DeviceManager::GetInstance().GetDevice();

		auto& cl = executionContext.commandList;
        cl.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

        // Bind layout + pipeline
        cl.BindLayout(m_layout->GetHandle());
        cl.BindPipeline(m_pso->GetHandle());

        for (const auto& j : m_pending)
        {
            if (!j.srcTexture || !j.dstCubemap) continue;

            const uint32_t srcSrvIndex = j.srcTexture->GetSRVInfo(0).slot.index;

            const uint32_t groupSize = 8;
            const uint32_t gx = (skyboxRes + groupSize - 1) / groupSize;
            const uint32_t gy = (skyboxRes + groupSize - 1) / groupSize;

            for (uint32_t face = 0; face < 6; ++face)
            {
                const uint32_t dstFaceUavIndex =
                    j.dstCubemap->GetUAVShaderVisibleInfo(0, face).slot.index;

                // Root constants payload: [srcSrv, dstFaceUav, face, size]
                uint32_t pc[4] = { srcSrvIndex, dstFaceUavIndex, face, (uint32_t)skyboxRes };

                // Push constants to CS: (set=0, binding=0) matches b0, space0 in HLSL
                cl.PushConstants(rhi::ShaderStage::Compute,
                    /*set*/0, /*binding*/0,
                    /*dstOffset32*/0, /*num32*/4, pc);

                cl.Dispatch(gx, gy, 1);
            }
        }

        m_declaredResourcesChanged = true;
        m_pending.clear();

        return {};
    }

    bool DeclaredResourcesChanged() const override {
        return m_declaredResourcesChanged;
    }

    void Cleanup() override {
        // Cleanup if necessary
    }

private:
    struct Job {
        std::shared_ptr<PixelBuffer> srcTexture;
        std::shared_ptr<PixelBuffer> dstCubemap;
    };

    std::function<uint16_t()> getSkyboxResolution;
    std::vector<Job> m_pending;
    bool m_declaredResourcesChanged = true;

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
        auto result = dev.CreatePipelineLayout(ld, m_layout);
        if (!m_layout || !m_layout->IsValid()) throw std::runtime_error("EnvConvert: layout failed");
        m_layout->SetName("EnvConvert.ComputeLayout");

        ShaderInfoBundle sib;
        sib.computeShader = { L"shaders/envToCubemap.hlsl", L"CSMain", L"cs_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);

        rhi::SubobjLayout soLayout{ m_layout->GetHandle() };
        rhi::SubobjShader soCS{ rhi::ShaderStage::Compute, rhi::DXIL(compiled.computeShader.Get()), "CSMain" };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soCS),
        };
        result = dev.CreatePipeline(items, (uint32_t)std::size(items), m_pso);
        if (Failed(result)) {
            throw std::runtime_error("EnvConvert: PSO failed");
        }
        m_pso->SetName("EnvConvert.ComputePSO");
    }
};
