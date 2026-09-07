#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Render/PassBuilders.h"
#include "BuiltinResources.h"

struct PresentFrameData {};

class PresentPass : public org::TypedRenderGraphPass<PresentPass, PresentFrameData> {
public:
	void Declare(org::PassBuilder& builder) {
		builder.WithPresent(Builtin::Backbuffer);
	}
	PresentFrameData Prepare(const org::PassPrepareContext&) { return {}; }
	static void Record(const PresentFrameData&, org::PassRecordContext&) {}
};
