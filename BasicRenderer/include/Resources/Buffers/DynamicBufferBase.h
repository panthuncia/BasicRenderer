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

    ResourceAccessType GetCurrentAccessType() const override { return m_dataBuffer->GetCurrentAccessType(); }
    ResourceLayout GetCurrentLayout() const override { return m_dataBuffer->GetCurrentLayout(); }
    ResourceSyncState GetPrevSyncState() const override { return m_dataBuffer->GetPrevSyncState(); }
protected:
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
    std::vector<BufferView*> m_dirtyViews;
};