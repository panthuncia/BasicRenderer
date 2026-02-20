#pragma once

#include <cstdint>
#include <typeindex>
#include <rhi.h>
#include <DirectXMath.h>

#include "Render/ImmediateExecution/ImmediateCommandList.h"

struct IHostExecutionData {
	virtual ~IHostExecutionData() = default;
	virtual const void* TryGet(std::type_index t) const noexcept = 0;

	template<class T>
	const T* Get() const noexcept {
		return static_cast<const T*>(TryGet(std::type_index(typeid(T))));
	}
};

struct UpdateExecutionContext {
	UINT frameIndex = 0;
	UINT64 frameFenceValue = 0;
	float deltaTime = 0.0f;
	const IHostExecutionData* hostData = nullptr;
};

struct ImmediateExecutionContext {
	rhi::Device device;
	rg::imm::ImmediateCommandList list;
	UINT frameIndex = 0;
	const IHostExecutionData* hostData = nullptr;
};

struct PassExecutionContext {
	rhi::Device device;
	rhi::CommandList commandList;
	UINT frameIndex = 0;
	UINT64 frameFenceValue = 0;
	float deltaTime = 0.0f;
	const IHostExecutionData* hostData = nullptr;
};
