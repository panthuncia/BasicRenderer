#pragma once

struct DispatchMeshIndirectCommand {
	unsigned int perObjectBufferIndex;
	unsigned int perMeshBufferIndex;
	unsigned int perMeshInstanceBufferIndex;
	D3D12_DISPATCH_MESH_ARGUMENTS dispatchMeshArguments;
};