#pragma once

#include <directx/d3d12.h>
#include "wrl/client.h"
#include "Scene/Components.h"

class Scene;
class ObjectManager;
class MeshManager;
class IndirectCommandBufferManager;
class CameraManager;
class LightManager;
class EnvironmentManager;
class PixelBuffer;

class RenderContext {
public:
	Components::DrawStats drawStats;
	ObjectManager* objectManager;
	MeshManager* meshManager;
	IndirectCommandBufferManager* indirectCommandBufferManager;
	CameraManager* cameraManager;
    LightManager* lightManager;
	EnvironmentManager* environmentManager;
    Scene* currentScene;
    ID3D12Device* device;
    ID3D12GraphicsCommandList7* commandList;
	ID3D12CommandQueue* commandQueue;
    ID3D12DescriptorHeap* textureDescriptorHeap;
    ID3D12DescriptorHeap* samplerDescriptorHeap;
    ID3D12DescriptorHeap* rtvHeap;
    UINT rtvDescriptorSize;
	UINT dsvDescriptorSize;
    UINT frameIndex;
	UINT64 frameFenceValue;
    DirectX::XMUINT2 renderResolution;
	DirectX::XMUINT2 outputResolution;
    unsigned int globalPSOFlags;
};