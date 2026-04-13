#include "Render/GraphExtensions/ClusterLOD/FixedSliceScalarVBOITEarlyDepthPass.h"

#include "BuiltinResources.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"

#include "../shaders/PerPassRootConstants/clodFixedSliceScalarVBOITEarlyDepthRootConstants.h"

FixedSliceScalarVBOITEarlyDepthPass::FixedSliceScalarVBOITEarlyDepthPass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture,
    std::shared_ptr<PixelBuffer> earlyDepthTexture)
    : m_configBuffer(std::move(configBuffer))
    , m_zeroTransmittanceSliceTexture(std::move(zeroTransmittanceSliceTexture))
    , m_earlyDepthTexture(std::move(earlyDepthTexture))
{
    auto dev = DeviceManager::GetInstance().GetDevice();

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSNoViewRayMain", L"vs_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/ClusterLOD/FixedSliceScalarVBOITEarlyDepth.hlsl", L"FixedSliceScalarVBOITEarlyDepthPSMain", L"ps_6_6" };
    auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);

    auto& layout = PSOManager::GetInstance().GetRootSignature();
    rhi::SubobjLayout soLayout{ layout.GetHandle() };
    rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(compiledBundle.vertexShader.Get()), "FullscreenVSNoViewRayMain" };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel, rhi::DXIL(compiledBundle.pixelShader.Get()), "FixedSliceScalarVBOITEarlyDepthPSMain" };

    rhi::RasterState rasterState{};
    rasterState.fill = rhi::FillMode::Solid;
    rasterState.cull = rhi::CullMode::None;
    rasterState.frontCCW = false;
    rhi::SubobjRaster soRaster{ rasterState };

    rhi::BlendState blendState{};
    blendState.alphaToCoverage = false;
    blendState.independentBlend = false;
    blendState.numAttachments = 0;
    rhi::SubobjBlend soBlend{ blendState };

    rhi::DepthStencilState depthState{};
    depthState.depthEnable = true;
    depthState.depthWrite = true;
    depthState.depthFunc = rhi::CompareOp::Always;
    rhi::SubobjDepth soDepth{ depthState };

    rhi::RenderTargets renderTargets{};
    renderTargets.count = 0;
    rhi::SubobjRTVs soRTV{ renderTargets };
    rhi::SubobjDSV soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample soSample{ rhi::SampleDesc{ 1, 0 } };
    rhi::SubobjPrimitiveTopology soTopology{ rhi::PrimitiveTopology::TriangleStrip };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soVS),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSample),
        rhi::Make(soTopology),
    };

    rhi::PipelinePtr pso;
    auto result = dev.CreatePipeline(items, static_cast<uint32_t>(std::size(items)), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create CLod fixed-slice scalar VBOIT early depth PSO");
    }

    pso->SetName("CLod.FixedSliceScalarVBOITEarlyDepth.PSO");
    m_pso = PipelineState(std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots);
}

void FixedSliceScalarVBOITEarlyDepthPass::DeclareResourceUsages(RenderPassBuilder* builder)
{
    builder->WithShaderResource(Builtin::CameraBuffer, m_configBuffer, m_zeroTransmittanceSliceTexture)
        .WithDepthReadWrite(m_earlyDepthTexture);
    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
}

void FixedSliceScalarVBOITEarlyDepthPass::Setup()
{
}

void FixedSliceScalarVBOITEarlyDepthPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

PassReturn FixedSliceScalarVBOITEarlyDepthPass::Execute(PassExecutionContext& executionContext)
{
    if (!m_configBuffer || !m_zeroTransmittanceSliceTexture || !m_earlyDepthTexture) {
        return {};
    }

    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

    rhi::PassBeginInfo passInfo{};
    rhi::DepthAttachment depthAttachment{};
    depthAttachment.dsv = m_earlyDepthTexture->GetDSVInfo(0).slot;
    depthAttachment.depthLoad = rhi::LoadOp::Clear;
    depthAttachment.depthStore = rhi::StoreOp::Store;
    depthAttachment.stencilLoad = rhi::LoadOp::DontCare;
    depthAttachment.stencilStore = rhi::StoreOp::DontCare;
    depthAttachment.clear = m_earlyDepthTexture->GetClearColor();
    passInfo.depth = &depthAttachment;
    passInfo.width = m_earlyDepthTexture->GetWidth();
    passInfo.height = m_earlyDepthTexture->GetHeight();
    passInfo.debugName = "CLod fixed-slice scalar VBOIT early depth";
    commandList.BeginPass(passInfo);

    commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleStrip);
    commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t misc[NumMiscUintRootConstants] = {};
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_EARLY_DEPTH_CONFIG_DESCRIPTOR_INDEX] = m_configBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_FIXED_SLICE_SCALAR_VBOIT_EARLY_DEPTH_ZERO_SLICE_DESCRIPTOR_INDEX] =
        m_zeroTransmittanceSliceTexture->GetSRVInfo(0).slot.index;
    commandList.PushConstants(
        rhi::ShaderStage::Pixel,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        misc);

    commandList.Draw(3, 1, 0, 0);
    return {};
}

void FixedSliceScalarVBOITEarlyDepthPass::Cleanup()
{
}