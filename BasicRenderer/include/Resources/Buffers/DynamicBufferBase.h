#pragma once

#include <memory>


#include "Render/RenderContext.h"
#include "Resources/GloballyIndexedResource.h"
#include "Resources/Buffers/GpuBufferBacking.h"

class BufferView;

class DynamicBufferBase : public GloballyIndexedResource {
public:
    DynamicBufferBase() {}
	// Engine representation of a GPU buffer- owns a handle to the actual GPU resource.
    std::unique_ptr<GpuBufferBacking> m_dataBuffer = nullptr;

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