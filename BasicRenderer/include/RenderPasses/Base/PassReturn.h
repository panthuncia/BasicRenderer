#pragma once

struct PassReturn {
	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;
};
