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