#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Render/PassBuilders.h"
#include "BuiltinResources.h"

class PresentPass : public RenderPass {
public:
	void DeclareResourceUsages(RenderPassBuilder* builder) override {
		builder->WithPresent(Builtin::Backbuffer);
	}

	void Setup() override {}
	void Cleanup() override {}
};