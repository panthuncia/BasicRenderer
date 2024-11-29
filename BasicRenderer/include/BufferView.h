#pragma once
#include <typeindex>
#include <memory>

#include "DynamicBuffer.h"

class BufferView {
public:
	static std::shared_ptr<BufferView> CreateShared(ViewedDynamicBufferBase* buffer, size_t offset, size_t size, std::type_index type) {
		return std::shared_ptr<BufferView>(new BufferView(buffer, offset, size, type));
	}
	static std::unique_ptr<BufferView> CreateUnique(ViewedDynamicBufferBase* buffer, size_t offset, size_t size, std::type_index type) {
		return std::unique_ptr<BufferView>(new BufferView(buffer, offset, size, type));
	}

    size_t GetOffset() const { return m_offset; }
    size_t GetSize() const { return m_size; }
	ViewedDynamicBufferBase* GetBuffer() const { return m_buffer; }

private:
    BufferView(ViewedDynamicBufferBase* buffer, size_t offset, size_t size, std::type_index type)
        : m_buffer(buffer), m_offset(offset), m_size(size), m_type(type) {
    }
    ViewedDynamicBufferBase* m_buffer;
    size_t m_offset;
    size_t m_size;
    std::type_index m_type;
};