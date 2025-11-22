#pragma once

#include <memory>
#include <flecs.h>
#include <cstdint>

#include "Resources/Resource.h"
#include "Render/RenderPhase.h"

namespace Components {
	
	struct Resource {
		std::weak_ptr<::Resource> resource;
	};
	
	struct IsIndirectArguments {};
	struct IsActiveDrawSetIndices {};

	struct BelongsToView {};
	struct ParticipatesInPass {};
}