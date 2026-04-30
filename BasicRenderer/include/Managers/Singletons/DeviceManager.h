#pragma once

#include <rhi.h>
#include <rhi_allocator.h>

namespace br {
class DeviceManager {
public:
    static DeviceManager& GetInstance();

    void Initialize();
    void Cleanup();

    rhi::Device GetDevice() const {
        return m_device.Get();
    }

    rhi::Queue GetGraphicsQueue() const {
        return m_graphicsQueue;
    }

    rhi::Queue GetComputeQueue() const {
        return m_computeQueue;
    }

    rhi::Queue GetCopyQueue() const {
        return m_copyQueue;
    }

    rhi::Backend GetBackend() const {
        return m_backend;
    }

    bool GetMeshShadersSupported() const {
        return m_meshShadersSupported;
    }

private:
    DeviceManager() = default;

    void CheckGPUFeatures();

    rhi::DevicePtr m_device;
    rhi::Queue m_graphicsQueue;
    rhi::Queue m_computeQueue;
    rhi::Queue m_copyQueue;
    rhi::Backend m_backend = rhi::Backend::Null;
    bool m_meshShadersSupported = false;
};
}

using DeviceManager = br::DeviceManager;
