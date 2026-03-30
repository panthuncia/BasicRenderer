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
				outPasses.push_back(
					RenderGraph::ExternalPassDesc::Render("Builtin::Uploads", upload)
						.At(RenderGraph::ExternalInsertPoint::Begin(/*prio*/0)));
			}
		}

		// Mipmapping pass: immediately after uploads
		if (m_textureFactory) {
			if (auto mip = m_textureFactory->GetMipmappingPass()) {
				outPasses.push_back(
					RenderGraph::ExternalPassDesc::Compute("Builtin::Mipmapping", mip)
						.At(RenderGraph::ExternalInsertPoint::Begin(/*prio*/1)));
			}
		}

		// Readback pass: last
		if (m_readbackManager) {
			if (auto rb = m_readbackManager->GetReadbackPass()) {
				outPasses.push_back(
					RenderGraph::ExternalPassDesc::Render("Builtin::Readbacks", rb)
						.At(RenderGraph::ExternalInsertPoint::End(/*prio*/0)));
			}
		}
	}

	void GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) override {
		// Some systems enqueue uploads during GatherFramePasses() itself
		// (for example CLod streaming disk-IO completions materializing
		// geometry/chunk-table updates). The structural Builtin::Uploads pass
		// has already recorded by then, so without a second upload pass those
		// updates would slip to the next frame. Re-run the upload pass early in
		// the frame pass stage so newly queued uploads are visible this frame.
		if (auto* uploadService = rg.GetUploadService()) {
			if (auto upload = uploadService->GetUploadPass()) {
				outPasses.push_back(
					RenderGraph::ExternalPassDesc::Render("Builtin::LateUploads", upload)
						.At(RenderGraph::ExternalInsertPoint::After("Builtin::Uploads")));
			}
		}
	}

private:
	TextureFactory* m_textureFactory = nullptr; // non-owning
	rg::runtime::IUploadService* m_uploadService = nullptr; // non-owning
	br::ReadbackManager* m_readbackManager = nullptr; // non-owning
};
