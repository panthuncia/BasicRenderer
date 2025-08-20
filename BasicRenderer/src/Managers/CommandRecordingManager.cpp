#include "Managers/CommandRecordingManager.h"

#include <cassert>

using Microsoft::WRL::ComPtr;

thread_local CommandRecordingManager::ThreadState CommandRecordingManager::s_tls{};

static D3D12_COMMAND_LIST_TYPE kListTypeFor(QueueKind qk) {
    switch (qk) {
    case QueueKind::Graphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case QueueKind::Compute:  return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case QueueKind::Copy:     return D3D12_COMMAND_LIST_TYPE_COPY;
    default:                  return D3D12_COMMAND_LIST_TYPE_DIRECT;
    }
}

CommandRecordingManager::CommandRecordingManager(const Init& init) {
    m_bind[static_cast<size_t>(QueueKind::Graphics)] =
    { init.graphicsQ, init.graphicsF, init.graphicsPool, D3D12_COMMAND_LIST_TYPE_DIRECT };

    m_bind[static_cast<size_t>(QueueKind::Compute)] =
    { init.computeQ,  init.computeF,  init.computePool,  D3D12_COMMAND_LIST_TYPE_COMPUTE };

    m_bind[static_cast<size_t>(QueueKind::Copy)] =
    { init.copyQ,     init.copyF,     init.copyPool,     D3D12_COMMAND_LIST_TYPE_COPY };

    m_computeMode = init.computeMode;
}

QueueKind CommandRecordingManager::resolve(QueueKind qk) const {
    if (qk == QueueKind::Compute && m_computeMode == ComputeMode::AliasToGraphics)
        return QueueKind::Graphics;
    return qk;
}

ID3D12GraphicsCommandList10* CommandRecordingManager::EnsureOpen(QueueKind requested, uint32_t frameEpoch) {
    const QueueKind qk = resolve(requested);
    auto& bind = m_bind[static_cast<size_t>(qk)];
    assert(bind.valid() && "Queue/Fence/Pool not initialized for this QueueKind");

    auto& tls = s_tls;
    auto& ctx = tls.ctxs[static_cast<size_t>(qk)];

    // If the epoch changed since last list, drop the old one (we'll get a new allocator)
    if (ctx.list && ctx.epoch != frameEpoch) {
        // Not strictly necessary to flush here; render graph should Flush at boundaries.
        // We just invalidate so next EnsureOpen will acquire a fresh pair.
        ctx.reset_soft();
    }

    if (!ctx.list) {
        // Acquire a fresh pair from the pool; Request() must return a reset & ready list
        CommandListPair pair = bind.pool->Request();

        // Defensive
#ifndef NDEBUG
        auto qtype = bind.queue->GetDesc().Type;
        auto& listIfc = pair.list; // ID3D12GraphicsCommandList10
        ComPtr<ID3D12GraphicsCommandList> base;
        listIfc.As(&base);
        assert(kListTypeFor(qk) == qtype && "Queue type mismatch");
        // We can't query the CL type directly, but Pool should have created with the right type.
#endif

        ctx.alloc = std::move(pair.allocator);
        ctx.list = std::move(pair.list);
        ctx.epoch = frameEpoch;
        ctx.dirty = true;
    }

    return ctx.list.Get();
}

uint64_t CommandRecordingManager::Flush(QueueKind requested, Signal sig) {
    const QueueKind qk = resolve(requested);
    auto& bind = m_bind[static_cast<size_t>(qk)];
    auto& ctx = s_tls.ctxs[static_cast<size_t>(qk)];

    uint64_t signaled = 0;

    if (ctx.list) {
        if (ctx.dirty) {
            // Close + execute
            ctx.list->Close();
            ID3D12CommandList* lists[] = { ctx.list.Get() };
            bind.queue->ExecuteCommandLists(1, lists);
        }

        // Decide on signaling
        if (sig.enable) {
            signaled = sig.value;
            bind.queue->Signal(bind.fence, signaled);
        }

        // Return the pair to the pool tagged with the fence (0 = immediately reusable)
        uint64_t recycleFence = sig.enable ? signaled : 0;

        // Hand back allocator/list to pool
        CommandListPair back;
        back.allocator = std::move(ctx.alloc);
        back.list = std::move(ctx.list);
        bind.pool->Recycle(std::move(back), recycleFence);

        // Invalidate thread-local context so next EnsureOpen() acquires a fresh pair
        ctx.reset_soft();
    }

    return signaled;
}

void CommandRecordingManager::EndFrame() {
    // Let pools reclaim any in-flight allocators whose fences have completed
    for (size_t i = 0; i < static_cast<size_t>(QueueKind::Count); ++i) {
        auto& bind = m_bind[i];
        if (!bind.valid()) continue;
        const uint64_t done = bind.fence->GetCompletedValue();
        bind.pool->RecycleCompleted(done);
    }
}

ID3D12Fence* CommandRecordingManager::Fence(QueueKind qk) const {
    qk = const_cast<CommandRecordingManager*>(this)->resolve(qk);
    return m_bind[static_cast<size_t>(qk)].fence;
}

ID3D12CommandQueue* CommandRecordingManager::Queue(QueueKind qk) const {
    qk = const_cast<CommandRecordingManager*>(this)->resolve(qk);
    return m_bind[static_cast<size_t>(qk)].queue;
}
