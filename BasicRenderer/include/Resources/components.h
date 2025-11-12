#pragma once

#include <memory>
#include <flecs.h>
#include <cstdint>

#include "Resources/Resource.h"
#include "Render/RenderPhase.h"

namespace Components {
	
	struct Resource {
		std::shared_ptr<::Resource> resource;
	};
	
	struct IsIndirectArguments {};

	struct BelongsToView { };

	struct ParticipatesInPass {};
}