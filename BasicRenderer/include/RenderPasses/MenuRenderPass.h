#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Render/RenderContext.h"
#include "Menu/Menu.h"

struct MenuFrameData {
	std::shared_ptr<const PreparedImGuiDrawData> drawData;
	DirectX::XMUINT2 outputResolution{};
};

class MenuRenderPass final : public org::TypedRenderGraphPass<MenuRenderPass, MenuFrameData> {
public:
	void Declare(org::PassBuilder& builder) {
		builder.WithRenderTarget(Builtin::Backbuffer);
	}

	MenuFrameData Prepare(const org::PassPrepareContext& preparation) {
		const auto* context = preparation.preparationData
			? preparation.preparationData->Get<RenderContext>() : nullptr;
		if (!context) return {};
		return {
			.drawData = Menu::GetInstance().PrepareDrawData(*context),
			.outputResolution = context->outputResolution,
		};
	}

	static void Record(const MenuFrameData& data, org::PassRecordContext& recording) {
		if (data.drawData) Menu::RecordPreparedDrawData(
			*data.drawData, recording.Commands(),
			recording.Resolve(org::ExternalBindingKey::SwapchainColor), data.outputResolution);
	}
};
