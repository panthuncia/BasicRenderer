#pragma once

#include <rhi_debug.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include <boost/container_hash/hash.hpp>

struct HierarchialCullingPassInputs {
	bool isFirstPass;

	friend bool operator==(const HierarchialCullingPassInputs&, const HierarchialCullingPassInputs&) = default;
};

inline rg::Hash64 HashValue(const HierarchialCullingPassInputs& i) {
	std::size_t seed = 0;

	boost::hash_combine(seed, i.isFirstPass);
	return seed;
}

class HierarchialCullingPass : public ComputePass {
public:
    HierarchialCullingPass(HierarchialCullingPassInputs inputs) {
        CreateWorkGraph(
            DeviceManager::GetInstance().GetDevice(),
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
			m_workGraph);
	}

	~HierarchialCullingPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) {
		
	}

	void Setup() override {

	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

		// Set the descriptor heaps
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

		
		return {};
	}

	void Cleanup() override {

	}

private:
	PipelineResources m_pipelineResources;
	rhi::WorkGraphPtr m_workGraph;

    rhi::Result CreateWorkGraph(
        rhi::Device device,
        rhi::PipelineLayoutHandle globalRootSignature,
        rhi::WorkGraphPtr& outGraph)
    {
        // Compile the work-graph library
		ShaderLibraryInfo libInfo(L"shaders/workGraphCulling.hlsl", L"lib_6_8");
        auto compiled = PSOManager::GetInstance().CompileShaderLibrary(libInfo);
		m_pipelineResources = compiled.resourceDescriptorSlots;

        rhi::ShaderBinary libDxil{
            compiled.libraryBlob.Get(),
            static_cast<uint32_t>(compiled.libraryBlob->GetBufferSize())
        };

        // Export the node shader symbols from the library
        // These are the *export names* (function symbols), not NodeID strings.
        std::array<rhi::ShaderExportDesc, 5> exports = { {
            { "WG_ObjectCull",   nullptr },
            { "WG_Traverse",     nullptr },
            { "WG_ClusterCull",  nullptr },
            { "WG_OcclusionCull",nullptr },
            { "WG_Rasterize",    nullptr },
        } };

        rhi::ShaderLibraryDesc library{};
        library.dxil = libDxil;
        library.exports = rhi::Span<rhi::ShaderExportDesc>(exports.data(), static_cast<uint32_t>(exports.size()));

        std::array<rhi::ShaderLibraryDesc, 1> libraries = { library };

        // Entry point is by NodeID (the [NodeID("ObjectCull")] in HLSL)
        std::array<rhi::NodeIDDesc, 1> entrypoints = { {
            { "ObjectCull", 0 }
        } };

        // Build the work graph desc
        rhi::WorkGraphDesc wg{};
        wg.programName = "HierarchialCulling";
        wg.flags = rhi::WorkGraphFlags::IncludeAllAvailableNodes; // quick iteration
        wg.globalRootSignature = globalRootSignature;
        wg.libraries = rhi::Span<rhi::ShaderLibraryDesc>(libraries.data(), static_cast<uint32_t>(libraries.size()));
        wg.entrypoints = rhi::Span<rhi::NodeIDDesc>(entrypoints.data(), static_cast<uint32_t>(entrypoints.size()));
        wg.allowStateObjectAdditions = false;
        wg.debugName = "HierarchialCullingWG";

        // Create
        return device.CreateWorkGraph(wg, outGraph);
    }
};