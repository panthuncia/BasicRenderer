#pragma once

#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>
#include <typeinfo>
#include <string>
#include <typeinfo>
#include <typeindex>

#include "DeviceManager.h"
#include "Buffer.h"
#include "Resource.h"
#include "BufferHandle.h"
#include "DynamicBufferBase.h"

using Microsoft::WRL::ComPtr;
struct MemoryBlock {
    size_t offset; // Start offset within the buffer
    size_t size;   // Size of the block
    bool isFree;   // Whether the block is free or allocated
};

class BufferView;

class DynamicBuffer : public DynamicBufferBase {
public:

    static std::shared_ptr<DynamicBuffer> CreateShared(bool byteAddress, size_t elementSize, UINT id = 0, size_t capacity = 64, std::wstring name = L"") {
        return std::shared_ptr<DynamicBuffer>(new DynamicBuffer(byteAddress, elementSize, id, capacity, name));
    }

    std::unique_ptr<BufferView> Allocate(size_t size, std::type_index type);
    void Deallocate(const std::shared_ptr<BufferView>& view);

    void SetOnResized(const std::function<void(UINT, size_t, size_t, bool, std::shared_ptr<Buffer>& buffer)>& callback) {
        onResized = callback;
    }

    std::shared_ptr<Buffer>& GetBuffer() {
        return m_dataBuffer;
    }

    size_t Size() {
        return m_capacity;
    }

	void* GetMappedData() {
		return m_mappedData;
	}
protected:
    void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState) override {
        currentState = newState;
        m_dataBuffer->Transition(commandList, prevState, newState);
    }

private:
    DynamicBuffer(bool byteAddress, size_t elementSize, UINT id = 0, size_t size = 64*1024, std::wstring name = L"")
        : m_byteAddress(byteAddress), m_elementSize(elementSize), m_globalResizableBufferID(id), m_capacity(size), m_needsUpdate(false) {
        CreateBuffer(size);
        if (name != L"") {
            m_dataBuffer->SetName((m_name + L": " + name).c_str());
        }
        else {
            m_dataBuffer->SetName(m_name.c_str());
        }
    }

	size_t m_elementSize;
	bool m_byteAddress;

    void* m_mappedData = nullptr;

    size_t m_capacity;
    bool m_needsUpdate;

    UINT m_globalResizableBufferID;

    std::vector<MemoryBlock> m_memoryBlocks;

    std::function<void(UINT, size_t, size_t, bool, std::shared_ptr<Buffer>&)> onResized;
    inline static std::wstring m_name = L"DynamicStructuredBuffer";

    void CreateBuffer(size_t capacity);
    void GrowBuffer(size_t newSize);
};