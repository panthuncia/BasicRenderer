#pragma once
#include <array>
#include <cstdint>
#include <wrl/client.h>
#include <d3d12.h>
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
        ID3D12CommandQueue* graphicsQ = nullptr;
        ID3D12Fence* graphicsF = nullptr;
        CommandListPool* graphicsPool = nullptr; // pool created with DIRECT

        ID3D12CommandQueue* computeQ = nullptr; // may be same as graphicsQ
        ID3D12Fence* computeF = nullptr;
        CommandListPool* computePool = nullptr; // pool created with COMPUTE

        ID3D12CommandQueue* copyQ = nullptr;
        ID3D12Fence* copyF = nullptr;
        CommandListPool* copyPool = nullptr; // pool created with COPY

        ComputeMode computeMode = ComputeMode::Async;
    };

    explicit CommandRecordingManager(const Init& init);

    // Get an open list for 'qk'. Creates one if needed, bound to 'frameEpoch'.
    ID3D12GraphicsCommandList10* EnsureOpen(QueueKind qk, uint32_t frameEpoch);

    // Close + Execute current list if dirty; optionally Signal. Returns the signaled value (or 0).
    uint64_t Flush(QueueKind qk, Signal sig = {});

    // Recycle allocators whose fences have completed (once per frame).
    void EndFrame();

    ID3D12Fence* Fence(QueueKind qk) const;
    ID3D12CommandQueue* Queue(QueueKind qk) const;

    // For aliasing mode: set at frame begin
    void SetComputeMode(ComputeMode mode) { m_computeMode = mode; }

private:
    struct QueueBinding {
        ID3D12CommandQueue* queue = nullptr;
        ID3D12Fence* fence = nullptr;
        CommandListPool* pool = nullptr;
        D3D12_COMMAND_LIST_TYPE listType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        bool valid() const { return queue && fence && pool; }
    };

    // One per process; alias compute -> graphics in AliasToGraphics mode dynamically
    std::array<QueueBinding, static_cast<size_t>(QueueKind::Count)> m_bind{};

    // Resolve backing queue for a requested logical QueueKind, given computeMode
    QueueKind resolve(QueueKind qk) const;

    //Per-thread recording state
    struct PerQueueCtx {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>   alloc;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList10> list;
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
