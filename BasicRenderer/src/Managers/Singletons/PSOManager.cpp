#include "Managers/Singletons/PSOManager.h"

#include <fstream>
#include <spdlog/spdlog.h>
#include <tree_sitter/api.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Materials/TechniqueDescriptor.h"
#include "brslHelpers.h"

#pragma comment(lib, "dxcompiler.lib")

void PSOManager::initialize() {
    createRootSignature();

    HMODULE dxcompiler = LoadLibrary(L"dxcompiler.dll");
    if (!dxcompiler)
    {

        throw std::runtime_error("Failed to load dxcompiler.dll");
    }

    // Get DxcCreateInstance function
    auto DxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(
        GetProcAddress(dxcompiler, "DxcCreateInstance"));
    if (!DxcCreateInstance)
    {
        throw std::runtime_error("Failed to get DxcCreateInstance function");
    }
    // Create compiler and library instances
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf()));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf()));
}

void PSOManager::Cleanup() {
    m_psoCache.clear();
    m_PPLLPSOCache.clear();
    m_meshPSOCache.clear();
    m_meshPPLLPSOCache.clear();
    m_prePassPSOCache.clear();
    m_meshPrePassPSOCache.clear();
    m_shadowPSOCache.clear();
    m_shadowMeshPSOCache.clear();
    m_visibilityBufferPSOCache.clear();
    m_visibilityBufferMeshPSOCache.clear();
    m_deferredPSOCache.clear();

    debugPSO.Reset();
    environmentConversionPSO.Reset();
    m_rootSignature.Reset();
    m_computeRootSignature.Reset();
    m_debugRootSignature.Reset();
    m_environmentConversionRootSignature.Reset();
    pUtils.Reset();
    pCompiler.Reset();
}

const PipelineState& PSOManager::GetPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_psoCache.find(key) == m_psoCache.end()) {
        m_psoCache[key] = CreatePSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_psoCache[key];
}

const PipelineState& PSOManager::GetShadowPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_shadowPSOCache.find(key) == m_shadowPSOCache.end()) {
        m_shadowPSOCache[key] = CreateShadowPSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_shadowPSOCache[key];
}

const PipelineState& PSOManager::GetShadowMeshPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_shadowMeshPSOCache.find(key) == m_shadowMeshPSOCache.end()) {
        m_shadowMeshPSOCache[key] = CreateShadowMeshPSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_shadowMeshPSOCache[key];
}

const PipelineState& PSOManager::GetPrePassPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_prePassPSOCache.find(key) == m_prePassPSOCache.end()) {
        m_prePassPSOCache[key] = CreatePrePassPSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_prePassPSOCache[key];
}

const PipelineState& PSOManager::GetMeshPrePassPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_meshPrePassPSOCache.find(key) == m_meshPrePassPSOCache.end()) {
        m_meshPrePassPSOCache[key] = CreateMeshPrePassPSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_meshPrePassPSOCache[key];
}

const PipelineState& PSOManager::GetPPLLPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_PPLLPSOCache.find(key) == m_PPLLPSOCache.end()) {
        m_PPLLPSOCache[key] = CreatePPLLPSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_PPLLPSOCache[key];
}

const PipelineState& PSOManager::GetMeshPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_meshPSOCache.find(key) == m_meshPSOCache.end()) {
        m_meshPSOCache[key] = CreateMeshPSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_meshPSOCache[key];
}

const PipelineState& PSOManager::GetMeshPPLLPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_meshPPLLPSOCache.find(key) == m_meshPPLLPSOCache.end()) {
        m_meshPPLLPSOCache[key] = CreateMeshPPLLPSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_meshPPLLPSOCache[key];
}

const PipelineState& PSOManager::GetVisibilityBufferPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_visibilityBufferPSOCache.find(key) == m_visibilityBufferPSOCache.end()) {
        m_visibilityBufferPSOCache[key] = CreateVisibilityBufferPSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_visibilityBufferPSOCache[key];
}

const PipelineState& PSOManager::GetVisibilityBufferMeshPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    PSOKey key(psoFlags, materialCompileFlags, wireframe);
    if (m_visibilityBufferMeshPSOCache.find(key) == m_visibilityBufferMeshPSOCache.end()) {
        m_visibilityBufferMeshPSOCache[key] = CreateVisibilityBufferMeshPSO(psoFlags, materialCompileFlags, wireframe);
    }
    return m_visibilityBufferMeshPSOCache[key];
}

const PipelineState& PSOManager::GetDeferredPSO(UINT psoFlags) {
    if (m_deferredPSOCache.find(psoFlags) == m_deferredPSOCache.end()) {
        m_deferredPSOCache[psoFlags] = CreateDeferredPSO(psoFlags);
    }
    return m_deferredPSOCache[psoFlags];
}

PipelineState PSOManager::CreatePSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe)
{
    auto defines = GetShaderDefines(psoFlags, materialCompileFlags);
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;

    auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
    vsBlob = compiledBundle.vertexShader;
    psBlob = compiledBundle.pixelShader;

    auto& layout = GetRootSignature();
    rhi::SubobjLayout soLayout{ layout.GetHandle()};

    rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(vsBlob.Get()) };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel,  rhi::DXIL(psBlob.Get()) };

    rhi::RasterState rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster soRaster{ rs };

    rhi::BlendState rhiBlend = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend soBlend{ rhiBlend };

    // Depth: Equal, no writes
    rhi::DepthStencilState ds{};
    ds.depthEnable = true;
    ds.depthWrite = false;                  // D3D12_DEPTH_WRITE_MASK_ZERO
    ds.depthFunc = rhi::CompareOp::Equal;  // D3D12_COMPARISON_FUNC_EQUAL
    rhi::SubobjDepth soDepth{ ds };

    rhi::RenderTargets rts{};
    if (psoFlags & PSOFlags::PSO_SHADOW) {
        rts.count = 0;
    }
    else {
        rts.count = 1;
        rts.formats[0] = rhi::Format::R16G16B16A16_Float;
    }
    rhi::SubobjRTVs soRTV{ rts };

    rhi::SubobjDSV    soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample soSmp{ rhi::SampleDesc{1,0} };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soVS),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSmp),
    };

    auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso; 
	auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create PSO (RHI)");
    }

    return { std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateShadowPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe)
{
    auto defines = GetShaderDefines(psoFlags, materialCompileFlags);

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;

    auto compiledBundle = CompileShaders(shaderInfoBundle);
    vsBlob = compiledBundle.vertexShader;
    psBlob = compiledBundle.pixelShader;

    auto& layout = GetRootSignature();
    rhi::SubobjLayout soLayout{ layout.GetHandle()};

    rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(vsBlob.Get()) };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel,  rhi::DXIL(psBlob.Get()) };

    rhi::RasterState rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster soRaster{ rs };

    rhi::BlendState rhiBlend = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend soBlend{ rhiBlend };

    rhi::DepthStencilState ds{};
    rhi::SubobjDepth soDepth{ ds };

    rhi::RenderTargets rts{};
    rts.count = 1;
    rts.formats[0] = rhi::Format::R32_Float;
    rhi::SubobjRTVs soRTV{ rts };

    rhi::SubobjDSV    soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample soSmp{ rhi::SampleDesc{1,0} };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soVS),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSmp),
    };

    auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso;
	auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create Shadow PSO (RHI)");
    }

    return { std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}


PipelineState PSOManager::CreatePrePassPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe)
{
    auto defines = GetShaderDefines(psoFlags | PSO_PREPASS, materialCompileFlags);

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/shaders.hlsl", L"VSMain",        L"vs_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PrepassPSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;

    auto compiledBundle = CompileShaders(shaderInfoBundle);
    vsBlob = compiledBundle.vertexShader;
    psBlob = compiledBundle.pixelShader;

    auto& layout = GetRootSignature(); // PipelineLayoutHandle
    rhi::SubobjLayout soLayout{ layout.GetHandle()};

    rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(vsBlob.Get()) };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel,  rhi::DXIL(psBlob.Get()) };

    rhi::RasterState rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster soRaster{ rs };

    rhi::BlendState rhiBlend = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend soBlend{ rhiBlend };

    rhi::DepthStencilState ds{}; // defaults: depth test on, write on, LessEqual
    rhi::SubobjDepth soDepth{ ds };

    rhi::RenderTargets rts{};
    rts.count = 6;
    rts.formats[0] = rhi::Format::R32G32B32A32_Float;   // Normals
    rts.formats[1] = rhi::Format::R16G16_Float;         // Motion vector
    rts.formats[2] = rhi::Format::R32_Float;            // Depth
    rts.formats[3] = rhi::Format::R8G8B8A8_UNorm;       // Albedo
    rts.formats[4] = rhi::Format::R8G8_UNorm;           // Metallic+Roughness
    rts.formats[5] = rhi::Format::R16G16B16A16_Float;   // Emissive

    rhi::SubobjRTVs soRTV{ rts };

    rhi::SubobjDSV    soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample soSmp{ rhi::SampleDesc{1,0} };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soVS),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSmp),
    };

    auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso;
	auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create PrePass PSO");
    }

    return { std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateVisibilityBufferPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe)
{
    auto defines = GetShaderDefines(psoFlags, materialCompileFlags);

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/shaders.hlsl", L"VisibilityBufferVSMain",        L"vs_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"VisibilityBufferPSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;

    auto compiledBundle = CompileShaders(shaderInfoBundle);
    vsBlob = compiledBundle.vertexShader;
    psBlob = compiledBundle.pixelShader;

    auto& layout = GetRootSignature(); // PipelineLayoutHandle
    rhi::SubobjLayout soLayout{ layout.GetHandle() };

    rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(vsBlob.Get()) };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel,  rhi::DXIL(psBlob.Get()) };

    rhi::RasterState rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster soRaster{ rs };

    rhi::BlendState rhiBlend = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend soBlend{ rhiBlend };

    rhi::DepthStencilState ds{}; // defaults: depth test on, write on, LessEqual
    rhi::SubobjDepth soDepth{ ds };

    rhi::RenderTargets rts{};

    rts.count = 1;
    rts.formats[0] = rhi::Format::R32G32_UInt; // packed visibility

    rhi::SubobjRTVs soRTV{ rts };

    rhi::SubobjDSV    soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample soSmp{ rhi::SampleDesc{1,0} };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soVS),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSmp),
    };

    auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso;
	auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create PrePass PSO");
    }

    return { std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateVisibilityBufferMeshPSO(
    UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    auto defines = GetShaderDefines(psoFlags, materialCompileFlags);

    Microsoft::WRL::ComPtr<ID3DBlob> asBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> msBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.amplificationShader = { L"shaders/amplification.hlsl", L"ASMain", L"as_6_6" };
    shaderInfoBundle.meshShader = { L"shaders/mesh.hlsl",          L"VisibilityBufferMSMain", L"ms_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl",       L"VisibilityBufferPSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;

    auto compiledBundle = CompileShaders(shaderInfoBundle);
    asBlob = compiledBundle.amplificationShader;
    msBlob = compiledBundle.meshShader;
    psBlob = compiledBundle.pixelShader;

    auto& layout = GetRootSignature();
    rhi::SubobjLayout soLayout{ layout.GetHandle() };
    rhi::SubobjShader soTask{ rhi::ShaderStage::Task, rhi::DXIL(asBlob.Get()) };
    rhi::SubobjShader soMesh{ rhi::ShaderStage::Mesh, rhi::DXIL(msBlob.Get()) };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel, rhi::DXIL(psBlob.Get()) };

    rhi::RasterState rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster soRaster{ rs };

    rhi::BlendState rhiBlend = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend soBlend{ rhiBlend };

    rhi::DepthStencilState ds{};
    rhi::SubobjDepth soDepth{ ds };

    rhi::RenderTargets rts{};
    
    rts.count = 1;
    rts.formats[0] = rhi::Format::R32G32_UInt; // packed visibility

    rhi::SubobjRTVs soRTV{ rts };

    rhi::SubobjDSV   soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample soSmp{ rhi::SampleDesc{1,0} };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soTask),
        rhi::Make(soMesh),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSmp),
    };

    auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso;
    auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create Mesh PrePass PSO");
    }

    return { std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreatePPLLPSO(UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe)
{
    auto defines = GetShaderDefines(psoFlags, materialCompileFlags);

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/PPLL.hlsl",    L"PPLLFillPS", L"ps_6_6" };
    shaderInfoBundle.defines = defines;

    auto compiledBundle = CompileShaders(shaderInfoBundle);
    vsBlob = compiledBundle.vertexShader;
    psBlob = compiledBundle.pixelShader;

    auto& layout = GetRootSignature(); // PipelineLayoutHandle
    rhi::SubobjLayout soLayout{ layout.GetHandle() };

    rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(vsBlob.Get()) };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel,  rhi::DXIL(psBlob.Get()) };

    rhi::RasterState rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster soRaster{ rs };

    rhi::BlendState rhiBlend = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend soBlend{ rhiBlend };

    rhi::DepthStencilState ds{};
    ds.depthEnable = true;
    ds.depthWrite = false; // D3D12_DEPTH_WRITE_MASK_ZERO
    rhi::SubobjDepth soDepth{ ds };

    rhi::RenderTargets rts{};
    rts.count = 1;
    rts.formats[0] = (psoFlags & PSOFlags::PSO_SHADOW)
        ? rhi::Format::R32_Float
        : rhi::Format::R16G16B16A16_Float;
    rhi::SubobjRTVs soRTV{ rts };

    rhi::SubobjDSV    soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample soSmp{ rhi::SampleDesc{1,0} };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soVS),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSmp),
    };

    auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso;
    auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create PPLL PSO");
    }

    return { std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateMeshPSO(
    UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe)
{
    auto defines = GetShaderDefines(psoFlags, materialCompileFlags);

    Microsoft::WRL::ComPtr<ID3DBlob> asBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> msBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.amplificationShader = { L"shaders/amplification.hlsl", L"ASMain", L"as_6_6" };
    shaderInfoBundle.meshShader = { L"shaders/mesh.hlsl",          L"MSMain", L"ms_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl",       L"PSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;

    auto compiledBundle = CompileShaders(shaderInfoBundle);
    asBlob = compiledBundle.amplificationShader;
    msBlob = compiledBundle.meshShader;
    psBlob = compiledBundle.pixelShader;

    auto& layout = GetRootSignature(); // your PipelineLayoutHandle
    rhi::SubobjLayout soLayout{ layout.GetHandle() };

    rhi::SubobjShader soTask{ rhi::ShaderStage::Task,  rhi::DXIL(asBlob.Get()) };
    rhi::SubobjShader soMesh{ rhi::ShaderStage::Mesh,  rhi::DXIL(msBlob.Get()) };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel, rhi::DXIL(psBlob.Get()) };

    rhi::RasterState rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster soRaster{ rs };

    rhi::BlendState rhiBlend = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend soBlend{ rhiBlend };

    rhi::DepthStencilState ds{};
    ds.depthEnable = true;
    ds.depthWrite = false;                   // D3D12_DEPTH_WRITE_MASK_ZERO
    ds.depthFunc = rhi::CompareOp::Equal;   // D3D12_COMPARISON_FUNC_EQUAL
    rhi::SubobjDepth soDepth{ ds };

    rhi::RenderTargets rts{};
    if (psoFlags & PSOFlags::PSO_SHADOW) {
        rts.count = 0;
    }
    else {
        rts.count = 1;
        rts.formats[0] = rhi::Format::R16G16B16A16_Float; // DXGI_FORMAT_R16G16B16A16_FLOAT
    }
    rhi::SubobjRTVs soRTV{ rts };

    rhi::SubobjDSV    soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample soSmp{ rhi::SampleDesc{1,0} };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soTask),
        rhi::Make(soMesh),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSmp),
    };

    auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso;
    auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create Mesh PSO");
    }

    return { std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateShadowMeshPSO(
    UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe)
{
    auto defines = GetShaderDefines(psoFlags, materialCompileFlags);

    Microsoft::WRL::ComPtr<ID3DBlob> asBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> msBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.amplificationShader = { L"shaders/amplification.hlsl", L"ASMain", L"as_6_6" };
    shaderInfoBundle.meshShader = { L"shaders/mesh.hlsl",          L"MSMain", L"ms_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl",       L"PSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;

    auto compiledBundle = CompileShaders(shaderInfoBundle);
    asBlob = compiledBundle.amplificationShader;
    msBlob = compiledBundle.meshShader;
    psBlob = compiledBundle.pixelShader;

    auto& layout = GetRootSignature();
    rhi::SubobjLayout soLayout{ layout.GetHandle() };

    rhi::SubobjShader soTask{ rhi::ShaderStage::Task,  rhi::DXIL(asBlob.Get()) };
    rhi::SubobjShader soMesh{ rhi::ShaderStage::Mesh,  rhi::DXIL(msBlob.Get()) };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel, rhi::DXIL(psBlob.Get()) };

    rhi::RasterState rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster soRaster{ rs };

    rhi::BlendState rhiBlend = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend soBlend{ rhiBlend };


    rhi::DepthStencilState ds{};
    rhi::SubobjDepth soDepth{ ds };

    // Render target formats
    rhi::RenderTargets rts{};
    rts.count = 1;
    rts.formats[0] = rhi::Format::R32_Float;
    rhi::SubobjRTVs soRTV{ rts };

    rhi::SubobjDSV    soDSV{ rhi::Format::D32_Float }; // DXGI_FORMAT_D32_FLOAT
    rhi::SubobjSample soSmp{ rhi::SampleDesc{1,0} };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soTask),
        rhi::Make(soMesh),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSmp),
    };

    auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso;
    auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create Shadow Mesh PSO");
    }

    return { std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateMeshPrePassPSO(
    UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    auto defines = GetShaderDefines(psoFlags | PSO_PREPASS, materialCompileFlags);

    Microsoft::WRL::ComPtr<ID3DBlob> asBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> msBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.amplificationShader = { L"shaders/amplification.hlsl", L"ASMain", L"as_6_6" };
    shaderInfoBundle.meshShader = { L"shaders/mesh.hlsl",          L"MSMain", L"ms_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl",       L"PrepassPSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;

    auto compiledBundle = CompileShaders(shaderInfoBundle);
    asBlob = compiledBundle.amplificationShader;
    msBlob = compiledBundle.meshShader;
    psBlob = compiledBundle.pixelShader;

	auto& layout = GetRootSignature();
    rhi::SubobjLayout soLayout{ layout.GetHandle() };
    rhi::SubobjShader soTask{ rhi::ShaderStage::Task, rhi::DXIL(asBlob.Get()) };
    rhi::SubobjShader soMesh{ rhi::ShaderStage::Mesh, rhi::DXIL(msBlob.Get()) };
    rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel, rhi::DXIL(psBlob.Get()) };

    rhi::RasterState rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster soRaster{ rs };

    rhi::BlendState rhiBlend = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend soBlend{ rhiBlend };

    rhi::DepthStencilState ds{};
    rhi::SubobjDepth soDepth{ ds };

    rhi::RenderTargets rts{};
    rts.count = 6;
    rts.formats[0] = rhi::Format::R32G32B32A32_Float;   // Normals
    rts.formats[1] = rhi::Format::R16G16_Float;         // Motion vector
    rts.formats[2] = rhi::Format::R32_Float;            // Depth
    rts.formats[3] = rhi::Format::R8G8B8A8_UNorm;       // Albedo
    rts.formats[4] = rhi::Format::R8G8_UNorm;           // Metallic+Roughness
    rts.formats[5] = rhi::Format::R16G16B16A16_Float;   // Emissive

    rhi::SubobjRTVs soRTV{ rts };

    rhi::SubobjDSV   soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample soSmp{ rhi::SampleDesc{1,0} };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soTask),
        rhi::Make(soMesh),
        rhi::Make(soPS),
        rhi::Make(soRaster),
        rhi::Make(soBlend),
        rhi::Make(soDepth),
        rhi::Make(soRTV),
        rhi::Make(soDSV),
        rhi::Make(soSmp),
    };

	auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso;
    auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create Mesh PrePass PSO");
    }

    return { std::move(pso), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateMeshPPLLPSO(
    UINT psoFlags, MaterialCompileFlags materialCompileFlags, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags, materialCompileFlags);

    // Compile shaders
	Microsoft::WRL::ComPtr<ID3DBlob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    
    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.amplificationShader = { L"shaders/amplification.hlsl", L"ASMain", L"as_6_6" };
    shaderInfoBundle.meshShader = { L"shaders/mesh.hlsl", L"MSMain", L"ms_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/PPLL.hlsl", L"PPLLFillPS", L"ps_6_6" };
    shaderInfoBundle.defines = defines;
    auto compiledBundle = CompileShaders(shaderInfoBundle);
    amplificationShader = compiledBundle.amplificationShader;
    meshShader = compiledBundle.meshShader;
	pixelShader = compiledBundle.pixelShader;

    auto& layout = GetRootSignature();
    rhi::SubobjLayout    soLayout{ layout.GetHandle() };

    rhi::SubobjShader    soAS{ rhi::ShaderStage::Task, { amplificationShader->GetBufferPointer(), (uint32_t)amplificationShader->GetBufferSize() } };
    rhi::SubobjShader    soMS{ rhi::ShaderStage::Mesh, { meshShader->GetBufferPointer(),          (uint32_t)meshShader->GetBufferSize() } };
    rhi::SubobjShader    soPS{ rhi::ShaderStage::Pixel, { pixelShader->GetBufferPointer(),         (uint32_t)pixelShader->GetBufferSize() } };

    rhi::RasterState     rs{};
    rs.fill = wireframe ? rhi::FillMode::Wireframe : rhi::FillMode::Solid;
    rs.cull = (materialCompileFlags & MaterialCompileFlags::MaterialCompileDoubleSided) ? rhi::CullMode::None : rhi::CullMode::Back;
    rs.frontCCW = true;
    rhi::SubobjRaster    soRaster{ rs };

    rhi::BlendState      bs = GetBlendDesc(materialCompileFlags);
    rhi::SubobjBlend     soBlend{ bs };

    rhi::DepthStencilState ds{};
    ds.depthEnable = true;
    ds.depthWrite = true;
    ds.depthFunc = rhi::CompareOp::LessEqual; // default
    rhi::SubobjDepth     soDepth{ ds };

    rhi::RenderTargets   rts{};
    rts.count = 6;
    rts.formats[0] = rhi::Format::R32G32B32A32_Float;
    rts.formats[1] = rhi::Format::R16G16_Float;
    rts.formats[2] = rhi::Format::R32_Float;
    rts.formats[3] = rhi::Format::R8G8B8A8_UNorm;
    rts.formats[4] = rhi::Format::R8G8_UNorm;
    rts.formats[5] = rhi::Format::R16G16B16A16_Float;

    rhi::SubobjRTVs      soRTV{ rts };

    rhi::SubobjDSV       soDSV{ rhi::Format::D32_Float };
    rhi::SubobjSample    soSmp{ rhi::SampleDesc{1,0} };

    rhi::PipelineStreamItem items[] = {
      rhi::Make(soLayout),
      rhi::Make(soAS), rhi::Make(soMS), rhi::Make(soPS),
      rhi::Make(soRaster), rhi::Make(soBlend), rhi::Make(soDepth),
      rhi::Make(soRTV), rhi::Make(soDSV), rhi::Make(soSmp),
    };
	auto device = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr psoPrepass;

    auto result = device.CreatePipeline(items, (uint32_t)std::size(items), psoPrepass);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create Mesh PrePass PSO");
    }

    return { std::move(psoPrepass), compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateDeferredPSO(UINT psoFlags)
{
    auto defines = GetShaderDefines(psoFlags, MaterialCompileFlags::MaterialCompileNone);

    PipelineState pso = MakeComputePipeline(
        GetComputeRootSignature().GetHandle(),
        L"shaders/deferred.hlsl",
        L"DeferredCSMain",
        defines,
        "DeferredComputePSO"
    );
    return pso;
}

PipelineState PSOManager::MakeComputePipeline(rhi::PipelineLayoutHandle layout,
    const wchar_t* shaderPath,
    const wchar_t* entryPoint,
    std::vector<DxcDefine> defines,
    const char* debugName)
{
    ShaderInfoBundle sib;
    sib.computeShader = { shaderPath, entryPoint, L"cs_6_6" };
    sib.defines = std::vector<DxcDefine>(defines.begin(), defines.end());
    auto compiled = CompileShaders(sib);

    rhi::SubobjLayout soLayout{ layout };
    rhi::SubobjShader soCS{ rhi::ShaderStage::Compute, rhi::DXIL(compiled.computeShader.Get()) };

    const rhi::PipelineStreamItem items[] = {
        rhi::Make(soLayout),
        rhi::Make(soCS),
    };

    auto dev = DeviceManager::GetInstance().GetDevice();
    rhi::PipelinePtr pso;
    auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pso);
    if (Failed(result)) {
        throw std::runtime_error("Failed to create compute PSO (RHI)");
    }
    if (debugName) pso->SetName(debugName);

    PipelineState out{
        std::move(pso),
        compiled.resourceIDsHash,
        compiled.resourceDescriptorSlots
    };
    return out;
}

std::vector<DxcDefine> PSOManager::GetShaderDefines(UINT psoFlags, MaterialCompileFlags materialFlags) {
    std::vector<DxcDefine> defines = {};
    if (materialFlags & MaterialCompileFlags::MaterialCompileDoubleSided) {
		DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"PSO_DOUBLE_SIDED";
		defines.insert(defines.begin(), macro);
    }
    if (materialFlags & MaterialCompileFlags::MaterialCompileAlphaTest) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_ALPHA_TEST";
        defines.insert(defines.begin(), macro);
    }
    if (materialFlags & MaterialCompileFlags::MaterialCompileBlend) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_BLEND";
        defines.insert(defines.begin(), macro);
    }
    if (materialFlags & MaterialCompileFlags::MaterialCompileBaseColorTexture) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_BASE_COLOR_TEXTURE";
		defines.insert(defines.begin(), macro);
	}
    if (materialFlags & MaterialCompileFlags::MaterialCompileParallax) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_PARALLAX";
        defines.insert(defines.begin(), macro);
	}
    if (materialFlags & MaterialCompileFlags::MaterialCompileNormalMap) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_NORMAL_MAP";
        defines.insert(defines.begin(), macro);
	}
	if (materialFlags & MaterialCompileFlags::MaterialCompileEmissiveTexture) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_EMISSIVE_TEXTURE";
        defines.insert(defines.begin(), macro);
    }
    if (materialFlags & MaterialCompileFlags::MaterialCompilePBRMaps) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_PBR_MAPS";
        defines.insert(defines.begin(), macro);
	}
    if (materialFlags & MaterialCompileFlags::MaterialCompileAOTexture) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_AO_TEXTURE";
        defines.insert(defines.begin(), macro);
	}

    if (psoFlags & PSOFlags::PSO_SHADOW) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_SHADOW";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::PSO_IMAGE_BASED_LIGHTING) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_IMAGE_BASED_LIGHTING";
        defines.insert(defines.begin(), macro);
    }
	if (psoFlags & PSOFlags::PSO_CLUSTERED_LIGHTING) {
		DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"PSO_CLUSTERED_LIGHTING";
		defines.insert(defines.begin(), macro);
	}
	if (psoFlags & PSOFlags::PSO_PREPASS) {
		DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"PSO_PREPASS";
		defines.insert(defines.begin(), macro);
	}
    if (psoFlags & PSOFlags::PSO_DEFERRED) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_DEFERRED";
        defines.insert(defines.begin(), macro);
	}
    if (!(psoFlags & PSOFlags::PSO_SCREENSPACE_REFLECTIONS)) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_SPECULAR_IBL";
        defines.insert(defines.begin(), macro);
	}

    return defines;
}

void PSOManager::GetPreprocessedBlob(
    const std::wstring& filename,
    const std::wstring& entryPoint,
    const std::wstring& target,
    std::vector<DxcDefine> defines,
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob) {

    auto exePath = std::filesystem::path(GetExePath());
    auto fullPath = exePath / filename;
    auto shaderDir = exePath / L"shaders";

    PSOManager::SourceData srcBuf;
    LoadSource(fullPath, srcBuf);
    auto includeHandler = CreateIncludeHandler();

    ShaderCompileOptions opts;
    opts.entryPoint = entryPoint;
    opts.target = target;
    opts.defines = std::move(defines);
#if BUILD_TYPE == BUILD_TYPE_DEBUG || BUILD_TYPE == BUILD_TYPE_RELEASE_DEBUG
    opts.enableDebugInfo = true;
#endif
    opts.warningsAsErrors = true;

    auto args = BuildArguments(opts, shaderDir);

    args.push_back(L"-P"); // Preprocess only
    auto preProcessedResult = InvokeCompile(
        srcBuf.buffer, args, includeHandler.Get()
    );

    preProcessedResult->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(&outBlob), nullptr);
}

void pruneUnusedCodeForSlot(
	std::string& inOutSource,
    const std::optional<ShaderInfo>& slot)
{
    if (!slot)
        return;
    // Prune unused code
    std::string prunedSource = pruneUnusedCode(
        inOutSource.c_str(),
        inOutSource.length(),
        ws2s(slot->entryPoint),
        {}
    );
	inOutSource = std::move(prunedSource);
}

void parseBRSLResourceIdentifiersForSlot(
    const std::optional<ShaderInfo>& slot,
    const std::vector<DxcDefine>& defines,
	const DxcBuffer* preprocessedBuffer,
    std::unordered_set<std::string>& outMandatoryIDs,
    std::unordered_set<std::string>& outOptionalIDs)
{
    if (!slot)
        return;
    
    ParseBRSLResourceIdentifiers(outMandatoryIDs, outOptionalIDs, preprocessedBuffer, ws2s(slot->entryPoint));
}

std::string rewriteResourceDescriptorIndexCallsForSlot(
    const std::optional<ShaderInfo>& slot,
    const std::vector<DxcDefine>& defines,
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
    DxcBuffer& outBuf,
    const std::unordered_map<std::string, std::string>& replacementMap)
{
    if (!slot)
        return "";
    return rewriteResourceDescriptorCalls(
        static_cast<const char*>(outBuf.Ptr),
        outBuf.Size,
        ws2s(slot->entryPoint),
        replacementMap
	);
}

void PSOManager::CompileShaderForSlot(
    const std::optional<ShaderInfo>& slot,
    const std::vector<DxcDefine>& defines,
    const DxcBuffer& buffer,
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob)
{
    if (!slot)
        return;
    //DxcBuffer ppBuffer = {};
    //Microsoft::WRL::ComPtr<ID3DBlob> preprocessedBlob;
    //PreprocessShaderSlot(slot, defines, preprocessedBlob, ppBuffer);
    CompileShader(
        slot->filename,
        slot->entryPoint,
        slot->target,
        buffer,
        defines,
        outBlob
    );
}

ShaderLibraryBundle PSOManager::CompileShaderLibrary(const ShaderLibraryInfo& libraryInfo, const std::vector<DxcDefine>& defines) {
    Microsoft::WRL::ComPtr<ID3DBlob> outBlob;
    DxcBuffer dxcPreprocessBuff;

	// Preprocess
    GetPreprocessedBlob(
        libraryInfo.filename,
        L"",
        libraryInfo.target,
        defines,
        outBlob
	);
    dxcPreprocessBuff.Ptr = outBlob->GetBufferPointer();
    dxcPreprocessBuff.Size = outBlob->GetBufferSize();
    dxcPreprocessBuff.Encoding = 0;

    // Compile BRSL info
    PreprocessedLibraryResult libPP = PreprocessShaderLibrary(dxcPreprocessBuff);

    DxcBuffer finalBuf = dxcPreprocessBuff;
    finalBuf.Ptr = libPP.finalSource.data();
    finalBuf.Size = libPP.finalSource.size();

    // compile as a library target (lib_6_8) without entry point
    CompileShader(libraryInfo.filename, /*entryPoint*/ L"", libraryInfo.target, finalBuf, defines, outBlob);

	std::vector<ResourceIdentifier> mandatoryResourceDescriptors;
    for (const auto& idStr : libPP.mandatoryIDs) {
		mandatoryResourceDescriptors.push_back(ResourceIdentifier{ idStr });
    }
	std::vector<ResourceIdentifier> optionalResourceDescriptors;
    for (const auto& idStr : libPP.optionalIDs) {
        optionalResourceDescriptors.push_back(ResourceIdentifier{ idStr });
	}

    ShaderLibraryBundle bundle;
	bundle.libraryBlob = outBlob;
    bundle.resourceDescriptorSlots = {mandatoryResourceDescriptors, optionalResourceDescriptors};
	bundle.resourceIDsHash = libPP.resourceIDsHash;
	return bundle;

}

ShaderBundle PSOManager::CompileShaders(const ShaderInfoBundle& info) {
    if (info.vertexShader && info.meshShader) 
		throw std::runtime_error("Cannot compile both vertex and mesh shaders in the same bundle");
	if (info.computeShader && (info.meshShader || info.amplificationShader || info.vertexShader || info.pixelShader))
		throw std::runtime_error("Cannot compile compute shader with other shader types in the same bundle");

	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedAmplificationShader;
	DxcBuffer amplificationBuffer = {};
	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedMeshShader;
	DxcBuffer meshBuffer = {};
	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedPixelShader;
	DxcBuffer pixelBuffer = {};
	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedVertexShader;
	DxcBuffer vertexBuffer = {};
	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedComputeShader;
	DxcBuffer computeBuffer = {};

    PreprocessShaderSlot(info.amplificationShader, info.defines, preprocessedAmplificationShader, amplificationBuffer);
    PreprocessShaderSlot(info.meshShader, info.defines, preprocessedMeshShader, meshBuffer);
    PreprocessShaderSlot(info.pixelShader, info.defines, preprocessedPixelShader, pixelBuffer);
    PreprocessShaderSlot(info.vertexShader, info.defines, preprocessedVertexShader, vertexBuffer);
    PreprocessShaderSlot(info.computeShader, info.defines, preprocessedComputeShader, computeBuffer);

	std::unordered_set<std::string> usedMandatoryIDs;
	std::unordered_set<std::string> usedOptionalIDs;
    parseBRSLResourceIdentifiersForSlot(info.amplificationShader, info.defines, &amplificationBuffer, usedMandatoryIDs, usedOptionalIDs);
	parseBRSLResourceIdentifiersForSlot(info.meshShader, info.defines, &meshBuffer, usedMandatoryIDs, usedOptionalIDs);
    parseBRSLResourceIdentifiersForSlot(info.pixelShader, info.defines, &pixelBuffer, usedMandatoryIDs, usedOptionalIDs);
    parseBRSLResourceIdentifiersForSlot(info.vertexShader, info.defines, &vertexBuffer, usedMandatoryIDs, usedOptionalIDs);
    parseBRSLResourceIdentifiersForSlot(info.computeShader, info.defines, &computeBuffer, usedMandatoryIDs, usedOptionalIDs);

	std::unordered_map<std::string, std::string> replacementMap;
	uint32_t nextIndex = 0;
    ShaderBundle bundle = {};
    std::vector<std::string> usedMandatoryResourceIDsVec;
    for (std::string entry : usedMandatoryIDs) {
		bundle.resourceDescriptorSlots.mandatoryResourceDescriptorSlots.push_back(entry);
		replacementMap[entry] = "ResourceDescriptorIndex" + std::to_string(nextIndex++);
        usedMandatoryResourceIDsVec.push_back(entry);
    }

	std::vector<std::string> usedOptionalResourceIDsVec;
    for (std::string entry : usedOptionalIDs) {
        bundle.resourceDescriptorSlots.optionalResourceDescriptorSlots.push_back(entry);
        replacementMap[entry] = "ResourceDescriptorIndex" + std::to_string(nextIndex++);
        usedOptionalResourceIDsVec.push_back(entry);
    }


	auto newAmplification = rewriteResourceDescriptorIndexCallsForSlot(info.amplificationShader, info.defines, preprocessedAmplificationShader, amplificationBuffer, replacementMap);
    auto newMesh = rewriteResourceDescriptorIndexCallsForSlot(info.meshShader, info.defines, preprocessedMeshShader, meshBuffer, replacementMap);
    auto newPixel = rewriteResourceDescriptorIndexCallsForSlot(info.pixelShader, info.defines, preprocessedPixelShader, pixelBuffer, replacementMap);
    auto newVertex = rewriteResourceDescriptorIndexCallsForSlot(info.vertexShader, info.defines, preprocessedVertexShader, vertexBuffer, replacementMap);
	auto newCompute = rewriteResourceDescriptorIndexCallsForSlot(info.computeShader, info.defines, preprocessedComputeShader, computeBuffer, replacementMap);

	pruneUnusedCodeForSlot(newAmplification, info.amplificationShader);
	pruneUnusedCodeForSlot(newMesh, info.meshShader);
	pruneUnusedCodeForSlot(newPixel, info.pixelShader);
	pruneUnusedCodeForSlot(newVertex, info.vertexShader);
	pruneUnusedCodeForSlot(newCompute, info.computeShader);

    if (!newAmplification.empty()) {
        amplificationBuffer.Ptr = newAmplification.data();
        amplificationBuffer.Size = newAmplification.size();
	}
    if (!newMesh.empty()) {
        meshBuffer.Ptr = newMesh.data();
        meshBuffer.Size = newMesh.size();
    }
    if (!newPixel.empty()) {
        pixelBuffer.Ptr = newPixel.data();
        pixelBuffer.Size = newPixel.size();
    }
    if (!newVertex.empty()) {
        vertexBuffer.Ptr = newVertex.data();
        vertexBuffer.Size = newVertex.size();
	}
    if (!newCompute.empty()) {
        computeBuffer.Ptr = newCompute.data();
        computeBuffer.Size = newCompute.size();
	}

	CompileShaderForSlot(info.amplificationShader, info.defines, amplificationBuffer, bundle.amplificationShader);
	CompileShaderForSlot(info.meshShader, info.defines, meshBuffer, bundle.meshShader);
	CompileShaderForSlot(info.pixelShader, info.defines, pixelBuffer, bundle.pixelShader);
	CompileShaderForSlot(info.vertexShader, info.defines, vertexBuffer, bundle.vertexShader);
    CompileShaderForSlot(info.computeShader, info.defines, computeBuffer, bundle.computeShader);

    std::vector<std::string> combinedIds = {};
	combinedIds.insert(combinedIds.end(), usedMandatoryResourceIDsVec.begin(), usedMandatoryResourceIDsVec.end());
	combinedIds.insert(combinedIds.end(), usedOptionalResourceIDsVec.begin(), usedOptionalResourceIDsVec.end());

	bundle.resourceIDsHash = hash_list(combinedIds);

	return bundle;
}

// for string delimiter
std::vector<std::string> split(std::string s, std::string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
        token = s.substr(pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back(token);
    }

    res.push_back(s.substr(pos_start));
    return res;
}
void PSOManager::CompileShader(
    const std::wstring& filename,
    const std::wstring& entryPoint, 
    const std::wstring& target, 
	const DxcBuffer& ppBuffer,
    std::vector<DxcDefine> defines,
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob)
{
    auto exePath = std::filesystem::path(GetExePath());
    auto fullPath = exePath / filename;
    auto shaderDir = exePath / L"shaders";

    ShaderCompileOptions opts;
    opts.entryPoint = entryPoint;
    opts.target = target;
    opts.defines = std::move(defines);
#if BUILD_TYPE == BUILD_TYPE_DEBUG || BUILD_TYPE == BUILD_TYPE_RELEASE_DEBUG
    opts.enableDebugInfo = true;
#endif
    opts.warningsAsErrors = true;

    auto args = BuildArguments(opts, shaderDir);

    ComPtr<IDxcIncludeHandler> includeHandler;
    HRESULT hr = pUtils->CreateDefaultIncludeHandler(&includeHandler);
    if (FAILED(hr)) {
        spdlog::error("Failed to create include handler.");
        ThrowIfFailed(hr);
        return;
    }
    auto result = InvokeCompile(ppBuffer, args, includeHandler.Get());

    ComPtr<IDxcBlobEncoding> errors;
    result->GetErrorBuffer(&errors);
    std::string errText{ (char*)errors->GetBufferPointer(),
                         errors->GetBufferSize() };

    auto obj = ExtractObject(result.Get(), filename, opts.enableDebugInfo);

    ThrowIfFailed(
        result->GetResult(reinterpret_cast<IDxcBlob**>(outBlob.GetAddressOf()))
    );
}

void PSOManager::LoadSource(const std::filesystem::path& path, PSOManager::SourceData& sd) {
    UINT32 codePage = CP_UTF8;
    ThrowIfFailed(pUtils->LoadFile(
        path.c_str(), &codePage, &sd.blob
    ));
    sd.buffer.Ptr = sd.blob->GetBufferPointer();
    sd.buffer.Size = sd.blob->GetBufferSize();
    sd.buffer.Encoding = 0; // see below
}


ComPtr<IDxcIncludeHandler> PSOManager::CreateIncludeHandler()
{
    ComPtr<IDxcIncludeHandler> handler;
    ThrowIfFailed(pUtils->CreateDefaultIncludeHandler(&handler));
    return handler;
}

std::vector<LPCWSTR> PSOManager::BuildArguments(
    const ShaderCompileOptions& opts,
    const std::filesystem::path& shaderDir)
{
    std::vector<LPCWSTR> args;

	if (opts.entryPoint != L"") { // SM 6.8 libraries don't have entry points
        args.push_back(L"-E"); args.push_back(opts.entryPoint.c_str());
    }
    args.push_back(L"-T"); args.push_back(opts.target.c_str());

    if (opts.warningsAsErrors)
        args.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);

    if (opts.enableDebugInfo) {
        args.push_back(DXC_ARG_DEBUG);
        args.push_back(DXC_ARG_DEBUG_NAME_FOR_SOURCE);
        args.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
    }

    for (auto& def : opts.defines) {
        args.push_back(L"-D");
        args.push_back(def.Name);
    }

    // always include shaders folder
    args.push_back(L"-I");
    args.push_back(shaderDir.c_str());

    return args;
}

ComPtr<IDxcResult> PSOManager::InvokeCompile(
    const DxcBuffer& src,
    std::vector<LPCWSTR>& arguments,
    IDxcIncludeHandler* includeHandler)
{
    ComPtr<IDxcResult> result;
    HRESULT hr = pCompiler->Compile(
        &src,
        arguments.data(),
        (UINT)arguments.size(),
        includeHandler,
        IID_PPV_ARGS(result.GetAddressOf())
    );

    // on failure or errors, pull DXC_OUT_ERRORS and log
    if (FAILED(hr)) {
        ComPtr<IDxcBlobUtf8> errs;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errs.GetAddressOf()), nullptr);
        if (errs && errs->GetStringLength())
            spdlog::error("Shader compile error: {}", errs->GetStringPointer());
        ThrowIfFailed(hr);
    }

    {
        ComPtr<IDxcBlobUtf8> errs;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errs.GetAddressOf()), nullptr);
        if (errs && errs->GetStringLength()) {
            spdlog::error("Shader compile warnings: {}", errs->GetStringPointer());
            if (strstr(errs->GetStringPointer(), "error"))
                ThrowIfFailed(E_FAIL);
        }
    }

    return result;
}

ComPtr<IDxcBlob> PSOManager::ExtractObject(
    IDxcResult* result,
    const std::wstring& filename,
    bool writeDebugArtifacts)
{
    ComPtr<IDxcBlob> objectBlob;
    ThrowIfFailed(result->GetOutput(
        DXC_OUT_OBJECT,
        IID_PPV_ARGS(objectBlob.GetAddressOf()),
        nullptr));

    if (writeDebugArtifacts) {
        auto exePath = std::filesystem::path(GetExePath());
        auto outDir = exePath / L"CompiledShaders";
        std::filesystem::create_directories(outDir);

        // derive a base name from the suggested pdb path
        ComPtr<IDxcBlobUtf16> pdbPathBlob;
        ComPtr<IDxcBlob>     pdbBlob;
        ThrowIfFailed(result->GetOutput(
            DXC_OUT_PDB,
            IID_PPV_ARGS(pdbBlob.GetAddressOf()),
            pdbPathBlob.GetAddressOf()));

        auto suggested = pdbPathBlob->GetStringPointer();
        auto baseName = std::filesystem::path(suggested).stem().wstring();

        WriteDebugArtifacts(result, outDir, baseName);
    }

    return objectBlob;
}

void PSOManager::WriteDebugArtifacts(
    IDxcResult* result,
    const std::filesystem::path& outDir,
    const std::wstring& baseName)
{
    // write .bin
    ComPtr<IDxcBlob> pdbBlob;
    ComPtr<IDxcBlob> objBlob;
    ThrowIfFailed(result->GetOutput(
        DXC_OUT_PDB, IID_PPV_ARGS(pdbBlob.GetAddressOf()), nullptr));
    ThrowIfFailed(result->GetOutput(
        DXC_OUT_OBJECT, IID_PPV_ARGS(objBlob.GetAddressOf()), nullptr));

    auto write = [&](const std::filesystem::path& path, IDxcBlob* blob) {
        std::ofstream f(path, std::ios::binary);
        if (!f) spdlog::error("Failed to open {} for writing", path.string());
        else    f.write((char*)blob->GetBufferPointer(), blob->GetBufferSize());
        };

    write(outDir / (baseName + L".bin"), objBlob.Get());
    write(outDir / (baseName + L".pdb"), pdbBlob.Get());
}

void PSOManager::createRootSignature() {

    auto device = DeviceManager::GetInstance().GetDevice();

    rhi::PushConstantRangeDesc pcs[] = {
    { rhi::ShaderStage::All, NumPerObjectRootConstants, /*set*/0, /*binding*/0 },
    { rhi::ShaderStage::All, NumPerMeshRootConstants,   0, 1 },
    { rhi::ShaderStage::All, NumViewRootConstants,      0, 2 },
    { rhi::ShaderStage::All, NumSettingsRootConstants,  0, 3 },
    { rhi::ShaderStage::All, NumDrawInfoRootConstants,  0, 4 },
    { rhi::ShaderStage::All, NumTransparencyInfoRootConstants, 0, 5 },
    { rhi::ShaderStage::All, NumLightClusterRootConstants,      0, 6 },
    { rhi::ShaderStage::All, NumMiscUintRootConstants,          0, 7 },
    { rhi::ShaderStage::All, NumMiscFloatRootConstants,         0, 8 },
    { rhi::ShaderStage::All, NumResourceDescriptorIndicesRootConstants, 0, 9 },
    };

    rhi::SamplerDesc pointClamp = {
        .minFilter = rhi::Filter::Nearest,
        .magFilter = rhi::Filter::Nearest,
        .mipFilter = rhi::MipFilter::Nearest,
        .addressU = rhi::AddressMode::Clamp,
        .addressV = rhi::AddressMode::Clamp,
        .addressW = rhi::AddressMode::Clamp,
    };

    rhi::SamplerDesc linearClamp = {
        .minFilter = rhi::Filter::Linear,
        .magFilter = rhi::Filter::Linear,
        .mipFilter = rhi::MipFilter::Linear,
        .addressU = rhi::AddressMode::Clamp,
        .addressV = rhi::AddressMode::Clamp,
        .addressW = rhi::AddressMode::Clamp,
	};


    rhi::StaticSamplerDesc staticSamplers[] = {
    { .sampler = pointClamp, .visibility = rhi::ShaderStage::All, .set = 0, .binding = 0, .arrayCount = 1 },
    {.sampler = linearClamp, .visibility = rhi::ShaderStage::All, .set = 0, .binding = 1, .arrayCount = 1 }, // fill filter to linear in DX12 map
    };

    auto result = device.CreatePipelineLayout(
        rhi::PipelineLayoutDesc{
            .ranges = {},
            .pushConstants = rhi::Span<rhi::PushConstantRangeDesc>(pcs, std::size(pcs)),
            .staticSamplers = rhi::Span<rhi::StaticSamplerDesc>(staticSamplers, std::size(staticSamplers)),
            .flags = rhi::PipelineLayoutFlags::PF_AllowInputAssembler
        },
        m_rootSignature);

    result = device.CreatePipelineLayout(
        rhi::PipelineLayoutDesc{
            .ranges = {},
            .pushConstants = rhi::Span<rhi::PushConstantRangeDesc>(pcs, std::size(pcs)),
            .staticSamplers = rhi::Span<rhi::StaticSamplerDesc>(staticSamplers, std::size(staticSamplers)),
            .flags = rhi::PipelineLayoutFlags::PF_None
        },
        m_computeRootSignature);
}

const rhi::PipelineLayout& PSOManager::GetRootSignature() {
    return m_rootSignature.Get();
}

const rhi::PipelineLayout& PSOManager::GetComputeRootSignature() {
	return m_computeRootSignature.Get();
}

void PSOManager::ReloadShaders() {
    m_psoCache.clear();
	m_meshPSOCache.clear();
	m_deferredPSOCache.clear();
	m_PPLLPSOCache.clear();
	m_shadowPSOCache.clear();
	m_meshPrePassPSOCache.clear();
	m_prePassPSOCache.clear();
	m_shadowMeshPSOCache.clear();
    m_prePassPSOCache.clear();
}

rhi::BlendState PSOManager::GetBlendDesc(MaterialCompileFlags materialCompileFlags) {

    if (!(materialCompileFlags & MaterialCompileAlphaTest) && !(materialCompileFlags & MaterialCompileBlend)) {
        rhi::BlendState opaqueBlendDesc = {};
        opaqueBlendDesc.alphaToCoverage = FALSE;
        opaqueBlendDesc.independentBlend = FALSE;

        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            opaqueBlendDesc.attachments[i].enable = FALSE;
            opaqueBlendDesc.attachments[i].writeMask = rhi::ColorWriteEnable::All;
        }
        return opaqueBlendDesc;
    }
    if (materialCompileFlags & MaterialCompileAlphaTest) {
        rhi::BlendState maskBlendDesc = {};
        maskBlendDesc.alphaToCoverage = FALSE;
        maskBlendDesc.independentBlend = FALSE;

        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            maskBlendDesc.attachments[i].enable = FALSE; // No standard blending needed.
            maskBlendDesc.attachments[i].writeMask = rhi::ColorWriteEnable::All;
        }
        return maskBlendDesc;
    }
    if (materialCompileFlags & MaterialCompileBlend) {
        rhi::BlendState blendBlendDesc = {};
        blendBlendDesc.alphaToCoverage = FALSE;
        blendBlendDesc.independentBlend = FALSE;

        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            blendBlendDesc.attachments[i].enable = TRUE;
            blendBlendDesc.attachments[i].srcColor = rhi::BlendFactor::SrcAlpha;
            blendBlendDesc.attachments[i].dstColor = rhi::BlendFactor::InvSrcAlpha;
            blendBlendDesc.attachments[i].colorOp = rhi::BlendOp::Add;
            blendBlendDesc.attachments[i].srcAlpha = rhi::BlendFactor::One;
            blendBlendDesc.attachments[i].dstAlpha = rhi::BlendFactor::Zero;
            blendBlendDesc.attachments[i].alphaOp = rhi::BlendOp::Add;
            blendBlendDesc.attachments[i].writeMask = rhi::ColorWriteEnable::All;
        }
        return blendBlendDesc;
    }

    spdlog::warn("Blend state not set, defaulting to opaque");
    rhi::BlendState opaqueBlendDesc = {};
    opaqueBlendDesc.alphaToCoverage = FALSE;
    opaqueBlendDesc.independentBlend = FALSE;

    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
        opaqueBlendDesc.attachments[i].enable = FALSE;
        opaqueBlendDesc.attachments[i].writeMask = rhi::ColorWriteEnable::All;
    }
    return opaqueBlendDesc;
}