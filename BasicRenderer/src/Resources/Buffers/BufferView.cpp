#include "Resources/Buffers/BufferView.h"

#include "Resources/Buffers/DynamicBufferBase.h"

std::shared_ptr<ViewedDynamicBufferBase> BufferView::GetBuffer() const {
	return m_buffer.lock();
}
