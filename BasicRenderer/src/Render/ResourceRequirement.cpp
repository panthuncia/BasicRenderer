#include "Render/ResourceRequirements.h"

ResourceAndRange::ResourceAndRange(const std::shared_ptr<Resource>& pResource) {
	resource = pResource;
	range = {}; // full range
}
