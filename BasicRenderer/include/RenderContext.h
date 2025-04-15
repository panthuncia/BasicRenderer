#pragma once

#include <directx/d3d12.h>
#include "wrl/client.h"
#include "Components.h"

class Scene;
class ObjectManager;
class MeshManager;
class IndirectCommandBufferManager;
class CameraManager;
class LightManager;

class RenderContext {
public:
	Components::DrawStats drawStats;
	ObjectManager* objectManager;
	MeshManager* meshManager;
	IndirectCommandBufferManager* indirectCommandBufferManager;
	CameraManager* cameraManager;
    LightManager* lightManager;
    Scene* currentScene;
    ID3D12Device* device;
    ID3D12GraphicsCommandList* commandList;
	ID3D12CommandQueue* commandQueue;
    ID3D12DescriptorHeap* textureDescriptorHeap;
    ID3D12DescriptorHeap* samplerDescriptorHeap;
    ID3D12DescriptorHeap* rtvHeap;
    ID3D12DescriptorHeap* dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> *renderTargets;
    UINT rtvDescriptorSize;
	UINT dsvDescriptorSize;
    UINT frameIndex;
	UINT64 frameFenceValue;
    UINT xRes;
    UINT yRes;
    
};