#pragma once

struct IndirectCommand {
	unsigned int perObjectBufferIndex;
	unsigned int perMeshBufferIndex;
	D3D12_DISPATCH_MESH_ARGUMENTS dispatchMeshArguments;
};