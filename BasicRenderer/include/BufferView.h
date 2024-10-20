#pragma once
#include <typeindex>

#include "DynamicBuffer.h"

class BufferView {
public:
    BufferView(DynamicBuffer* buffer, size_t offset, size_t size, std::type_index type)
        : m_buffer(buffer), m_offset(offset), m_size(size), m_type(type) {
    }

    template<typename T>
    T* Map() {
        if (typeid(T) != m_type)
        {
            // Handle type mismatch error
            return nullptr;
        }

        // Return the pointer to the data at the offset
        uint8_t* dataPtr = reinterpret_cast<uint8_t*>(m_buffer->GetMappedData());
        return reinterpret_cast<T*>(dataPtr + m_offset);
    }

    void Unmap() {
        // No action needed since the buffer is persistently mapped
    }

    size_t GetOffset() const { return m_offset; }
    size_t GetSize() const { return m_size; }
	DynamicBuffer* GetBuffer() const { return m_buffer; }

private:
    DynamicBuffer* m_buffer;
    size_t m_offset;
    size_t m_size;
    std::type_index m_type;
};