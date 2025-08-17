#pragma once
#include <typeindex>
#include <memory>

class ViewedDynamicBufferBase;

class BufferView {
public:
	static std::shared_ptr<BufferView> CreateShared(ViewedDynamicBufferBase* buffer, uint64_t offset, uint64_t size, uint64_t elementSize) {
		return std::shared_ptr<BufferView>(new BufferView(buffer, offset, size, elementSize));
	}
	static std::unique_ptr<BufferView> CreateUnique(ViewedDynamicBufferBase* buffer, uint64_t offset, uint64_t size, uint64_t elementSize) {
		return std::unique_ptr<BufferView>(new BufferView(buffer, offset, size, elementSize));
	}

    uint64_t GetOffset() const { return m_offset; }
    uint64_t GetSize() const { return m_size; }
	ViewedDynamicBufferBase* GetBuffer() const { return m_buffer; }

private:
    BufferView(ViewedDynamicBufferBase* buffer, uint64_t offset, uint64_t size, uint64_t elementSize)
        : m_buffer(buffer), m_offset(offset), m_size(size), m_elementSize(elementSize) {
    }
    ViewedDynamicBufferBase* m_buffer;
    uint64_t m_offset;
    uint64_t m_size;
    uint64_t m_elementSize;
};