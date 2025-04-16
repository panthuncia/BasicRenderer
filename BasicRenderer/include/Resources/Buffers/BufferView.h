#pragma once
#include <typeindex>
#include <memory>

class ViewedDynamicBufferBase;

class BufferView {
public:
	static std::shared_ptr<BufferView> CreateShared(ViewedDynamicBufferBase* buffer, size_t offset, size_t size, size_t elementSize) {
		return std::shared_ptr<BufferView>(new BufferView(buffer, offset, size, elementSize));
	}
	static std::unique_ptr<BufferView> CreateUnique(ViewedDynamicBufferBase* buffer, size_t offset, size_t size, size_t elementSize) {
		return std::unique_ptr<BufferView>(new BufferView(buffer, offset, size, elementSize));
	}

    size_t GetOffset() const { return m_offset; }
    size_t GetSize() const { return m_size; }
	ViewedDynamicBufferBase* GetBuffer() const { return m_buffer; }

private:
    BufferView(ViewedDynamicBufferBase* buffer, size_t offset, size_t size, size_t elementSize)
        : m_buffer(buffer), m_offset(offset), m_size(size), m_elementSize(elementSize) {
    }
    ViewedDynamicBufferBase* m_buffer;
    size_t m_offset;
    size_t m_size;
    size_t m_elementSize;
};