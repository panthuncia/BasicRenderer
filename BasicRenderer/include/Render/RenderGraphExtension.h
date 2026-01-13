#pragma once

#include "Render/RenderGraph.h"

#include "Managers/Singletons/UploadManager.h"
#include "Managers/Singletons/ReadbackManager.h"
#include "Factories/TextureFactory.h"

// Registers "system" passes that live outside the RenderGraph, while keeping the graph
// unaware of the managers/factories that own them.
//
// order:
//   Uploads (first)
//   Mipmapping (immediately after uploads)
//   ...
//   Readbacks (last)
class RenderGraphIOExtension final : public RenderGraph::IRenderGraphExtension {
public:
	explicit RenderGraphIOExtension(TextureFactory* textureFactory)
		: m_textureFactory(textureFactory) {
	}

	void OnRegistryReset(ResourceRegistry* reg) override {
		UploadManager::UploadResolveContext ctx;
		ctx.registry = reg;

		//if constexpr (requires(ResourceRegistry const& r) { r.GetEpoch(); }) {
		//	ctx.epoch = rg.GetRegistry().GetEpoch();
		//}

		UploadManager::GetInstance().SetUploadResolveContext(ctx);
	}

	void GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override {
		(void)rg;

		// Upload pass: first
		if (auto upload = UploadManager::GetInstance().GetUploadPass()) {
			RenderGraph::ExternalPassDesc d;
			d.type = RenderGraph::PassType::Render;
			d.name = "Builtin::Uploads";
			d.where = RenderGraph::ExternalInsertPoint::Begin(/*prio*/0);
			d.pass = upload;
			outPasses.push_back(std::move(d));
		}

		// Mipmapping pass: immediately after uploads
		if (m_textureFactory) {
			if (auto mip = m_textureFactory->GetMipmappingPass()) {
				RenderGraph::ExternalPassDesc d;
				d.type = RenderGraph::PassType::Compute;
				d.name = "Builtin::Mipmapping";
				d.where = RenderGraph::ExternalInsertPoint::Begin(/*prio*/1);
				d.pass = mip;
				outPasses.push_back(std::move(d));
			}
		}

		// Readback pass: last
		if (auto rb = ReadbackManager::GetInstance().GetReadbackPass()) {
			RenderGraph::ExternalPassDesc d;
			d.type = RenderGraph::PassType::Render;
			d.name = "Builtin::Readbacks";
			d.where = RenderGraph::ExternalInsertPoint::End(/*prio*/0);
			d.pass = rb;
			outPasses.push_back(std::move(d));
		}
	}

private:
	TextureFactory* m_textureFactory = nullptr; // non-owning
};
