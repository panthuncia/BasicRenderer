#pragma once

struct IDynamicDeclaredResources {
	virtual bool DeclaredResourcesChanged() = 0;
	virtual ~IDynamicDeclaredResources() = default;
};