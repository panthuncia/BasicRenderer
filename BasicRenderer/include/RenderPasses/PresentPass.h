#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Render/PassBuilders.h"
#include "BuiltinResources.h"

class PresentPass : public org::TypedRenderGraphPass<PresentPass> {
public:
	void Declare(org::PassBuilder& builder) {
		builder.WithPresent(Builtin::Backbuffer);
	}
	static void Record(org::PassRecordContext&) {}
};
