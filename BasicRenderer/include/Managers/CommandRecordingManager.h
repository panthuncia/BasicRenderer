#pragma once
#include <array>
#include <cstdint>
#include <rhi.h>
#include "Render/CommandListPool.h"
#include "Render/QueueKind.h"

struct Signal {
    bool     enable = false;
    uint64_t value = 0; // if 0, manager will pick next monotonic
};

enum class ComputeMode : uint8_t { Async, AliasToGraphics };

class CommandRecordingManager {
public:
    struct Init {
        rhi::Queue* graphicsQ = nullptr;
        rhi::Timeline* graphicsF = nullptr;
        CommandListPool* graphicsPool = nullptr; // pool created with DIRECT

        rhi::Queue* computeQ = nullptr; // may be same as graphicsQ
        rhi::Timeline* computeF = nullptr;
        CommandListPool* computePool = nullptr; // pool created with COMPUTE

        rhi::Queue* copyQ = nullptr;
        rhi::Timeline* copyF = nullptr;
        CommandListPool* copyPool = nullptr; // pool created with COPY

        ComputeMode computeMode = ComputeMode::Async;
    };

    explicit CommandRecordingManager(const Init& init);

    // Get an open list for 'qk'. Creates one if needed, bound to 'frameEpoch'.
    rhi::CommandList EnsureOpen(QueueKind qk, uint32_t frameEpoch);

    // Close + Execute current list if dirty; optionally Signal. Returns the signaled value (or 0).
    uint64_t Flush(QueueKind qk, Signal sig = {});

    // Recycle allocators whose fences have completed (once per frame).
    void EndFrame();

    rhi::Timeline* Fence(QueueKind qk) const;
    rhi::Queue* Queue(QueueKind qk) const;

    // For aliasing mode: set at frame begin
    void SetComputeMode(ComputeMode mode) { m_computeMode = mode; }

private:
    struct QueueBinding {
        rhi::Queue* queue = nullptr;
        rhi::Timeline* fence = nullptr;
        CommandListPool* pool = nullptr;
        rhi::QueueKind listType = rhi::QueueKind::Graphics;
        bool valid() const { return queue && fence && pool; }
    };

    // One per process; alias compute -> graphics in AliasToGraphics mode dynamically
    std::array<QueueBinding, static_cast<size_t>(QueueKind::Count)> m_bind{};

    // Resolve backing queue for a requested logical QueueKind, given computeMode
    QueueKind resolve(QueueKind qk) const;

    //Per-thread recording state
    struct PerQueueCtx {
        rhi::CommandAllocatorPtr   alloc;
        rhi::CommandListPtr list;
        uint32_t epoch = ~0u;
        bool dirty = false;
        void reset_soft() { list.Reset(); alloc.Reset(); dirty = false; epoch = ~0u; }
    };

    struct ThreadState {
        std::array<PerQueueCtx, static_cast<size_t>(QueueKind::Count)> ctxs{};
        uint32_t cachedEpoch = ~0u; // to force rebind at new frame
    };

    static thread_local ThreadState s_tls;

    ComputeMode m_computeMode = ComputeMode::Async;
};
