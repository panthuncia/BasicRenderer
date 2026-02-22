#pragma once

#include "Render/RenderGraph/RenderGraph.h"

#include "Factories/TextureFactory.h"
#include "Managers/ReadbackManager.h"
#include "Render/Runtime/IUploadService.h"

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
	RenderGraphIOExtension(TextureFactory* textureFactory,
		rg::runtime::IUploadService* uploadService,
		br::ReadbackManager* readbackManager)
		: m_textureFactory(textureFactory),
		m_uploadService(uploadService),
		m_readbackManager(readbackManager) {
	}

	void OnRegistryReset(ResourceRegistry* reg) override {
		rg::runtime::UploadResolveContext ctx;
		ctx.registry = reg;
		ctx.epoch = 0; // TODO: Will this be useful?
		if (m_uploadService) {
			m_uploadService->SetUploadResolveContext(ctx);
		}
	}

	void GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override {
		(void)rg;

		// Upload pass: first
		if (m_uploadService) {
			if (auto upload = m_uploadService->GetUploadPass()) {
			RenderGraph::ExternalPassDesc d;
			d.type = RenderGraph::PassType::Render;
			d.name = "Builtin::Uploads";
			d.where = RenderGraph::ExternalInsertPoint::Begin(/*prio*/0);
			d.pass = upload;
			outPasses.push_back(std::move(d));
			}
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
		if (m_readbackManager) {
			if (auto rb = m_readbackManager->GetReadbackPass()) {
				RenderGraph::ExternalPassDesc d;
				d.type = RenderGraph::PassType::Render;
				d.name = "Builtin::Readbacks";
				d.where = RenderGraph::ExternalInsertPoint::End(/*prio*/0);
				d.pass = rb;
				outPasses.push_back(std::move(d));
			}
		}
	}

private:
	TextureFactory* m_textureFactory = nullptr; // non-owning
	rg::runtime::IUploadService* m_uploadService = nullptr; // non-owning
	br::ReadbackManager* m_readbackManager = nullptr; // non-owning
};
