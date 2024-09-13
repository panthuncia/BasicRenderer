#pragma once

#include "Resource.h"

class GloballyIndexedResource : public Resource
{
public:
	uint32_t GetIndex() {
		return index;
	}
protected:
	SetIndex(uint32_t index) {
		this->index = index;
	}
private:
	uint32_t index;
};