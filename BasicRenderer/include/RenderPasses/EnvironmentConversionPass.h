#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedEnvironmentDispatch.h"
#include "Managers/EnvironmentManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Managers/EnvironmentManager.h"
#include "Interfaces/IDynamicDeclaredResources.h"

#include <vector>

class EnvironmentConversionPass : public org::TypedRenderGraphPass<EnvironmentConversionPass, br::render::PreparedEnvironmentDispatch>, public IDynamicDeclaredResources {
public:
    EnvironmentConversionPass() {

        CreateEnvironmentConversionPSO();
    }

    void Declare(org::PassBuilder& builder) {
        builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
        for (const auto& j : m_pending) {
            if (!j->work.srcTexture || !j->work.dstCubemap) continue;

            builder.WithShaderResource(j->work.srcTexture);
            builder.WithUnorderedAccess(j->work.dstCubemap);
        }

        m_declaredResourcesChanged = false;
    }



    void Update(const UpdateExecutionContext& context) override {
        const auto* input = context.hostData->Get<UpdateContext>();
        if (input->environmentManager) m_work = input->environmentManager->GetConversionWorkQueue();
        auto pending = m_work.Pending();
        if (pending != m_pending) { m_pending = std::move(pending); m_declaredResourcesChanged = true; }
    }

    br::render::PreparedEnvironmentDispatch Prepare(const org::PassPrepareContext& preparation) {
        br::render::PreparedEnvironmentDispatch data;
        if (m_pending.empty()) return data;
        const auto* context = preparation.preparationData->Get<UpdateContext>();
        data.resourceHeap = context->textureDescriptorHeap.GetHandle();
        data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
        data.program = preparation.CaptureProgram(m_pso);
        data.constantCount = 4;
        for (const auto& entry : m_pending) {
            const auto& job = entry->work;
            const auto size = job.dstCubemap->GetWidth();
            const auto src = job.srcTexture->GetSRVInfo(0).slot.index;
            for (uint32_t face = 0; face < 6; ++face)
                data.faces.push_back({{src, job.dstCubemap->GetUAVShaderVisibleInfo(0, face).slot.index, face, size, 0}, (size + 7) / 8});
        }
        m_work.Reserve(m_pending, preparation);
        m_pending.clear(); m_declaredResourcesChanged = true;
        return data;
    }

    static void Record(const br::render::PreparedEnvironmentDispatch& data, org::PassRecordContext& recording) {
        br::render::RecordEnvironmentDispatch(data, recording);
    }

    bool DeclaredResourcesChanged() const override {
        return m_declaredResourcesChanged;
    }



private:
    EnvironmentManager::ConversionWorkQueue m_work;
    EnvironmentManager::ConversionWorkQueue::Snapshot m_pending;
    bool m_declaredResourcesChanged = true;

    PipelineState m_pso;

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
        auto layout = std::make_shared<rhi::PipelineLayoutPtr>();
        auto result = dev.CreatePipelineLayout(ld, *layout);
        if (!*layout || !layout->Get().IsValid()) throw std::runtime_error("EnvConvert: layout failed");
        layout->Get().SetName("EnvConvert.ComputeLayout");

        ShaderInfoBundle sib;
        sib.computeShader = { L"shaders/envToCubemap.hlsl", L"CSMain", L"cs_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);

        rhi::SubobjLayout soLayout{ layout->Get().GetHandle() };
        rhi::SubobjShader soCS{ rhi::ShaderStage::Compute, rhi::DXIL(compiled.computeShader.Get()), "CSMain" };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soCS),
        };
        rhi::PipelinePtr pipeline;
        result = dev.CreatePipeline(items, (uint32_t)std::size(items), pipeline);
        if (Failed(result)) {
            throw std::runtime_error("EnvConvert: PSO failed");
        }
        pipeline->SetName("EnvConvert.ComputePSO");
        m_pso = PipelineState(std::move(pipeline), compiled.resourceIDsHash,
            compiled.resourceDescriptorSlots, layout, soLayout.layout);
    }
};
