#pragma once

struct IDynamicDeclaredResources {
	virtual bool DeclaredResourcesChanged() const = 0;
	virtual ~IDynamicDeclaredResources() = default;
};