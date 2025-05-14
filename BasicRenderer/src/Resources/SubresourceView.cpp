#include "Resources/SubresourceView.h"

#include "Resources/Resource.h"

SubresourceView::SubresourceView(const Resource* resource) {
	this->resource = resource;
	range.firstMip = 0;
	range.mipCount = resource->GetMipLevels();
	range.firstSlice = 0;
	range.sliceCount = resource->GetArraySize();
}

SubresourceView::SubresourceView(const Resource* resource, uint32_t firstMip, uint32_t mipCount, uint32_t firstSlice, uint32_t sliceCount):
	resource(resource), range{ firstMip, mipCount, firstSlice, sliceCount } {
	if (firstMip + mipCount > resource->GetMipLevels()) {
		throw std::runtime_error("SubresourceView mip range out of bounds");
	}
	if (firstSlice + sliceCount > resource->GetArraySize()) {
		throw std::runtime_error("SubresourceView slice range out of bounds");
	}
}
