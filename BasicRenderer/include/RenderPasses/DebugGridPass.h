#pragma once

#include <cstdint>
#include <cstring>   // memcpy
#include <stdexcept>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "../shaders/PerPassRootConstants/debugGridRootConstants.h"

// Uses MiscUintRootSignatureIndex / UintRootConstantN in HLSL.

class DebugGridPass final : public ComputePass
{
public:
    struct Params
    {
        bool  enabled = true;

        // Grid plane: Y = planeY, grid aligned to world XZ.
        float planeY = 0.0f;

        // World-space cell sizes.
        float minorCellSize = 1.0f;
        float majorCellSize = 10.0f;

        // Line widths expressed as FULL fraction of the cell (0..1).
        // Example: 0.02 => 2% of cell width.
        float minorLineWidth = 0.02f;
        float majorLineWidth = 0.04f;

        // Axis line half-width in WORLD units (single line at X==0 and Z==0).
        float axisHalfWidthWorld = 0.03f;

        // Opacity scalars.
        float minorOpacity = 0.35f;
        float majorOpacity = 0.60f;
        float axisOpacity = 0.90f;

        // Master opacity multiplier for the entire pass.
        float overallOpacity = 1.0f;
    };

    explicit DebugGridPass(const Params& p = {})
        : m_params(p)
    {
        CreatePSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) override
    {
        builder
            ->WithShaderResource(Builtin::CameraBuffer,
                Builtin::PrimaryCamera::LinearDepthMap)
            // Needs UAV, since compute will read-modify-write (manual blend)
            .WithUnorderedAccess(Builtin::PostProcessing::UpscaledHDR);
    }

    void Setup() override
    {
        RegisterSRV(Builtin::CameraBuffer);
        RegisterSRV(Builtin::PrimaryCamera::LinearDepthMap);
        RegisterUAV(Builtin::PostProcessing::UpscaledHDR);
    }

    PassReturn Execute(PassExecutionContext& executionContext) override {
        auto* renderContext = executionContext.hostData ? const_cast<RenderContext*>(executionContext.hostData->Get<RenderContext>()) : nullptr;
        if (!renderContext) return {};
        auto& context = *renderContext;
        auto& psoManager = PSOManager::GetInstance();
        auto& cmd = context.commandList;

        cmd.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(),
            context.samplerDescriptorHeap.GetHandle());

        cmd.BindLayout(psoManager.GetComputeRootSignature().GetHandle());
        cmd.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());

        BindResourceDescriptorIndices(cmd, m_pso.GetResourceDescriptorSlots());

        // Root constants (packed as uint32; shader reads them via asfloat()).
        uint32_t rc[NumMiscUintRootConstants] = {};
        rc[RC_PlaneY] = PackFloat(m_params.planeY);
        rc[RC_MinorCellSize] = PackFloat(m_params.minorCellSize);
        rc[RC_MajorCellSize] = PackFloat(m_params.majorCellSize);
        rc[RC_MinorLineWidth] = PackFloat(m_params.minorLineWidth);
        rc[RC_MajorLineWidth] = PackFloat(m_params.majorLineWidth);
        rc[RC_AxisHalfWidthWorld] = PackFloat(m_params.axisHalfWidthWorld);
        rc[RC_MinorOpacity] = PackFloat(m_params.minorOpacity);
        rc[RC_MajorOpacity] = PackFloat(m_params.majorOpacity);
        rc[RC_AxisOpacity] = PackFloat(m_params.axisOpacity);
        rc[RC_OverallOpacity] = PackFloat(m_params.overallOpacity);

        cmd.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, rc);

        uint32_t w = context.outputResolution.x;
        uint32_t h = context.outputResolution.y;

        constexpr uint32_t groupSizeX = 8;
        constexpr uint32_t groupSizeY = 8;
        const uint32_t groupsX = (w + groupSizeX - 1) / groupSizeX;
        const uint32_t groupsY = (h + groupSizeY - 1) / groupSizeY;

        cmd.Dispatch(groupsX, groupsY, 1);
        return {};
    }

    void Cleanup() override {}

    Params& GetParams() { return m_params; }
    const Params& GetParams() const { return m_params; }

private:
    PipelineState m_pso;

    Params m_params;

    static uint32_t PackFloat(float v)
    {
        uint32_t u;
        static_assert(sizeof(u) == sizeof(v));
        std::memcpy(&u, &v, sizeof(u));
        return u;
    }

    void CreatePSO()
    {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"shaders/DebugGrid.hlsl",
            L"DebugGridCSMain",
            {},
            "DebugGridComputePSO"
        );
    }
};
