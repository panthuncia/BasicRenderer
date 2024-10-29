#pragma once

struct ShaderVisibleIndexInfo {
    int index = -1; // Index in the descriptor heap
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle; // CPU descriptor handle
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle; // GPU descriptor handle
};

struct NonShaderVisibleIndexInfo {
    int index = -1; // Index in the descriptor heap
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle; // CPU descriptor handle
};