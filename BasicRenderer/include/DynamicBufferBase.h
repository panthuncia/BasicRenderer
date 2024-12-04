#pragma once

#include <memory>

#include "RenderContext.h"
#include "ResourceStates.h"
#include "GloballyIndexedResource.h"

class Buffer;
class BufferView;

class Buffer;

class DynamicBufferBase : public GloballyIndexedResource {
public:
    DynamicBufferBase() {}
    std::shared_ptr<Buffer> m_dataBuffer = nullptr;
	Buffer* GetBuffer() { return m_dataBuffer.get(); }
protected:
    virtual void Transition(const RenderContext& context, ResourceState prevState, ResourceState newState) {};
    uint8_t m_numDataBuffers = 0;
};

class ViewedDynamicBufferBase : public DynamicBufferBase {
public:
    ViewedDynamicBufferBase() {}

    void MarkViewDirty(BufferView* view) {
        m_dirtyViews.push_back(view);
    }

	void ClearDirtyViews() {
		m_dirtyViews.clear();
	}

    const std::vector<BufferView*>& GetDirtyViews() {
        return m_dirtyViews;
    }

    virtual void UpdateView(BufferView* view, const void* data) = 0;

protected:
    virtual void Transition(const RenderContext& context, ResourceState prevState, ResourceState newState) {};
    std::vector<BufferView*> m_dirtyViews;
};