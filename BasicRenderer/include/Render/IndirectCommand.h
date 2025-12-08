#pragma once

#include <d3d12.h>

struct DispatchMeshIndirectCommand {
	unsigned int perObjectBufferIndex;
	unsigned int perMeshBufferIndex;
	unsigned int perMeshInstanceBufferIndex;
	D3D12_DISPATCH_MESH_ARGUMENTS dispatchMeshArguments;
};

struct DispatchIndirectCommand {
	unsigned int perObjectBufferIndex;
	unsigned int perMeshBufferIndex;
	unsigned int perMeshInstanceBufferIndex;
	D3D12_DISPATCH_ARGUMENTS dispatchArguments;
};

struct MaterialEvaluationIndirectCommand {
	// Root constants (all uints):
	unsigned int materialId; // UintRootConstant0
	unsigned int baseOffset; // UintRootConstant1
	unsigned int count; // UintRootConstant2
	D3D12_DISPATCH_ARGUMENTS dispatchArguments;
};