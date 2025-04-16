#pragma once

#include <memory>

#include "Render/RenderContext.h"
#include "Resources/ResourceStates.h"
#include "Resources/GloballyIndexedResource.h"

class Buffer;
class BufferView;

class Buffer;

class DynamicBufferBase : public GloballyIndexedResource {
public:
    DynamicBufferBase() {}
    std::shared_ptr<Buffer> m_dataBuffer = nullptr;
protected:
    virtual void Transition(const RenderContext& context, ResourceState prevState, ResourceState newState) {};
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