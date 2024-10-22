#pragma once

#include <wrl/client.h>
#include <directx/d3d12.h>
#include <vector>
#include <string>
#include <functional>

struct ReadbackRequest {
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
    UINT64 totalSize;
    std::wstring outputFile;
    std::function<void()> callback;
};