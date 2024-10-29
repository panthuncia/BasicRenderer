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
    std::shared_ptr<Buffer> m_uploadBuffer;
    std::shared_ptr<Buffer> m_dataBuffer;
protected:
    virtual void Transition(const RenderContext& context, ResourceState prevState, ResourceState newState) {};
};

class ViewedDynamicBufferBase : public DynamicBufferBase {
public:
    ViewedDynamicBufferBase() {}
    virtual void* GetMappedData() = 0;

    void MarkViewDirty(BufferView* view) {
        m_dirtyViews.push_back(view);
    }

	void ClearDirtyViews() {
		m_dirtyViews.clear();
	}

    const std::vector<BufferView*>& GetDirtyViews() {
        return m_dirtyViews;
    }

protected:
    virtual void Transition(const RenderContext& context, ResourceState prevState, ResourceState newState) {};
    std::vector<BufferView*> m_dirtyViews;
};