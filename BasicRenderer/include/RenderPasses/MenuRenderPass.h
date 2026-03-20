#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Render/RenderContext.h"
#include "Menu/Menu.h"

class MenuRenderPass : public RenderPass {
public:
	void DeclareResourceUsages(RenderPassBuilder* builder) override {
		builder->WithRenderTarget(Builtin::Backbuffer);
	}

	void Setup() override {}

	PassReturn Execute(PassExecutionContext& executionContext) override {
		auto* renderContext = executionContext.hostData->Get<RenderContext>();
		Menu::GetInstance().Render(*renderContext, executionContext.commandList);
		return {};
	}

	void Cleanup() override {}
};
