#pragma once

#include "Resource.h"

class GloballyIndexedResource : public Resource
{
public:
	GloballyIndexedResource(std::string name) : Resource(name){};
	GloballyIndexedResource() {};
	int GetIndex() {
		return index;
	}
protected:
	void SetIndex(int index) {
		this->index = index;
	}
private:
	int index = -1;
};