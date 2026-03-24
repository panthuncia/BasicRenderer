#pragma once

#include <filesystem>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Utilities/Utilities.h"
#include "Interfaces/IDynamicDeclaredResources.h"

#include <vector>

class EnvironmentFilterPass : public RenderPass, public IDynamicDeclaredResources {
public:
    EnvironmentFilterPass() {
        CreatePrefilterPSO();
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        for (const auto& j : m_pending) {
            if (!j.srcCubemap || !j.dstPrefilteredCubemap) continue;

            builder->WithShaderResource(j.srcCubemap);
            builder->WithUnorderedAccess(j.dstPrefilteredCubemap);
        }

        m_declaredResourcesChanged = false;
    }

    void Setup() override { }

    void Update(const UpdateExecutionContext& context) override {
        std::vector<Job> newPending;
        auto* updateData = context.hostData->Get<UpdateContext>();

        if (updateData->environmentManager) {
            auto environments = updateData->environmentManager->GetAndClearEnvironmentsToPrefilter();
            newPending.reserve(environments.size());

            for (auto* env : environments) {
                if (!env) continue;

                auto srcCubeAsset = env->GetEnvironmentCubemap();
                auto dstPrefilteredCube = env->GetEnvironmentPrefilteredCubemap();
                if (!srcCubeAsset || !dstPrefilteredCube) continue;

                auto srcCube = srcCubeAsset->ImagePtr();
                if (!srcCube) continue;

                Job j{};
                j.srcCubemap = srcCube;
                j.dstPrefilteredCubemap = dstPrefilteredCube;
                j.baseResolution = env->GetReflectionCubemapResolution();
                newPending.push_back(std::move(j));
            }
        }

        auto sameJobs = [](const std::vector<Job>& a, const std::vector<Job>& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (a[i].srcCubemap.get() != b[i].srcCubemap.get()) return false;
                if (a[i].dstPrefilteredCubemap.get() != b[i].dstPrefilteredCubemap.get()) return false;
                if (a[i].baseResolution != b[i].baseResolution) return false;
            }
            return true;
        };

        if (!sameJobs(m_pending, newPending)) {
            m_declaredResourcesChanged = true;
            m_pending = std::move(newPending);
        }
    }

    PassReturn Execute(PassExecutionContext& executionContext) override {
        auto* renderContext = executionContext.hostData->Get<RenderContext>();
        auto& context = *renderContext;
        if (m_pending.empty()) return {};

        auto dev = DeviceManager::GetInstance().GetDevice();

		auto& cl = executionContext.commandList;

        cl.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

        cl.BindLayout(m_layout->GetHandle());
        cl.BindPipeline(m_pso->GetHandle());

        for (const auto& j : m_pending)
        {
            if (!j.srcCubemap || !j.dstPrefilteredCubemap) continue;

            const uint32_t baseRes = j.baseResolution;
            const uint32_t srcSrvIndex = j.srcCubemap->GetSRVInfo(0).slot.index;

            const uint32_t group = 8;

            const uint32_t maxMipLevels = j.dstPrefilteredCubemap->GetNumUAVMipLevels();
            for (uint32_t mip = 0; mip < maxMipLevels; ++mip)
            {
                const uint32_t size = std::max(1u, baseRes >> mip);
                const uint32_t gx = (size + group - 1) / group;
                const uint32_t gy = (size + group - 1) / group;

                float roughness = (maxMipLevels > 1) ? (float)mip / float(maxMipLevels - 1) : 0.0f;

                for (uint32_t face = 0; face < 6; ++face)
                {
                    const uint32_t dstUavIndex =
                        j.dstPrefilteredCubemap->GetUAVShaderVisibleInfo(mip, face).slot.index;

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

        m_declaredResourcesChanged = true;
        m_pending.clear();

        return {};
    }

    bool DeclaredResourcesChanged() const override {
        return m_declaredResourcesChanged;
    }

    void Cleanup() override {}

private:
    struct Job {
        std::shared_ptr<PixelBuffer> srcCubemap;
        std::shared_ptr<PixelBuffer> dstPrefilteredCubemap;
        uint32_t baseResolution = 0;
    };

    std::vector<Job> m_pending;
    bool m_declaredResourcesChanged = true;

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
        auto result = dev.CreatePipelineLayout(ld, m_layout);
        if (!m_layout || !m_layout->IsValid()) throw std::runtime_error("EnvFilter: layout failed");
        m_layout->SetName("EnvFilter.ComputeLayout");

        // Compile compute shader
        ShaderInfoBundle sib;
        sib.computeShader = { L"shaders/blurEnvironment.hlsl", L"CSMain", L"cs_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);

        // Create compute PSO
        rhi::SubobjLayout soLayout{ m_layout->GetHandle() };
        rhi::SubobjShader soCS{ rhi::ShaderStage::Compute, rhi::DXIL(compiled.computeShader.Get()), "CSMain" };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soCS),
        };
        result = dev.CreatePipeline(items, (uint32_t)std::size(items), m_pso);
        if (Failed(result)) {
            throw std::runtime_error("EnvFilter: PSO failed");
        }
        m_pso->SetName("EnvFilter.ComputePSO");
    }
};
