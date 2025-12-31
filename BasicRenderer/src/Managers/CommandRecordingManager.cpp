#include "Managers/CommandRecordingManager.h"

#include <cassert>

using Microsoft::WRL::ComPtr;

thread_local CommandRecordingManager::ThreadState CommandRecordingManager::s_tls{};

CommandRecordingManager::CommandRecordingManager(const Init& init) {
    m_bind[static_cast<size_t>(QueueKind::Graphics)] =
    { init.graphicsQ, init.graphicsF, init.graphicsPool, rhi::QueueKind::Graphics };

    m_bind[static_cast<size_t>(QueueKind::Compute)] =
    { init.computeQ,  init.computeF,  init.computePool,  rhi::QueueKind::Compute };

    m_bind[static_cast<size_t>(QueueKind::Copy)] =
    { init.copyQ,     init.copyF,     init.copyPool,     rhi::QueueKind::Copy };

    m_computeMode = init.computeMode;
}

QueueKind CommandRecordingManager::resolve(QueueKind qk) const {
    if (qk == QueueKind::Compute && m_computeMode == ComputeMode::AliasToGraphics)
        return QueueKind::Graphics;
    return qk;
}

rhi::CommandList CommandRecordingManager::EnsureOpen(QueueKind requested, uint32_t frameEpoch) {
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
            ctx.list->End();
			bind.queue->Submit({ &ctx.list.Get(), 1 }, {});
        }

        // Decide on signaling
        if (sig.enable) {
            signaled = sig.value;
            bind.queue->Signal({ bind.fence->GetHandle(), signaled});
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

rhi::Timeline* CommandRecordingManager::Fence(QueueKind qk) const {
    qk = const_cast<CommandRecordingManager*>(this)->resolve(qk);
    return m_bind[static_cast<size_t>(qk)].fence;
}

rhi::Queue* CommandRecordingManager::Queue(QueueKind qk) const {
    qk = const_cast<CommandRecordingManager*>(this)->resolve(qk);
    return m_bind[static_cast<size_t>(qk)].queue;
}

void CommandRecordingManager::ShutdownThreadLocal() {
    auto& tls = s_tls;

    for (auto& ctx : tls.ctxs) {
        // NOTE: this does NOT Flush(). It just releases refs owned by TLS.
        // Only call this when you know there are no in-flight command lists
        // for this thread (after device idle).
        ctx.reset_soft();
    }

    tls.cachedEpoch = ~0u;
}