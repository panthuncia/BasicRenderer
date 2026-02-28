#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <enkiTS/TaskScheduler.h>

namespace br {

class TaskSchedulerManager {
public:
    static TaskSchedulerManager& GetInstance();

    void Initialize(uint32_t ioThreadCount = 2, uint32_t externalTaskThreads = 0);
    void Cleanup();
    void RunIoTask(std::function<void()>&& task);

    bool IsInitialized() const {
        return m_initialized;
    }

    enki::TaskScheduler& GetScheduler() {
        return m_scheduler;
    }

    const enki::TaskScheduler& GetScheduler() const {
        return m_scheduler;
    }

    uint32_t GetNumTaskThreads() const {
        return m_initialized ? m_scheduler.GetNumTaskThreads() : 0u;
    }

    uint32_t GetNumIoThreads() const {
        return static_cast<uint32_t>(m_ioThreadNumbers.size());
    }

    template <typename Func>
    void ParallelFor(size_t itemCount, Func&& func) {
        if (itemCount == 0) {
            return;
        }

        if (!m_initialized || itemCount == 1) {
            for (size_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
                func(itemIndex);
            }
            return;
        }

        using Callable = std::decay_t<Func>;
        Callable callable = std::forward<Func>(func);

        std::atomic<bool> stopRequested = false;
        std::exception_ptr workerException;
        std::mutex exceptionMutex;

        struct ParallelForTask final : enki::ITaskSet {
            ParallelForTask(
                Callable* callableIn,
                size_t baseIndexIn,
                size_t itemCountIn,
                std::atomic<bool>* stopRequestedIn,
                std::exception_ptr* workerExceptionIn,
                std::mutex* exceptionMutexIn)
                : callable(callableIn)
                , baseIndex(baseIndexIn)
                , itemCount(itemCountIn)
                , stopRequested(stopRequestedIn)
                , workerException(workerExceptionIn)
                , exceptionMutex(exceptionMutexIn) {
                m_SetSize = static_cast<uint32_t>(itemCount);
            }

            void ExecuteRange(enki::TaskSetPartition range, uint32_t) override {
                const size_t start = static_cast<size_t>(range.start);
                const size_t end = static_cast<size_t>(range.end);
                for (size_t localIndex = start; localIndex < end; ++localIndex) {
                    if (stopRequested->load(std::memory_order_acquire)) {
                        return;
                    }

                    const size_t itemIndex = baseIndex + localIndex;
                    try {
                        (*callable)(itemIndex);
                    }
                    catch (...) {
                        std::lock_guard<std::mutex> lock(*exceptionMutex);
                        if (!*workerException) {
                            *workerException = std::current_exception();
                            stopRequested->store(true, std::memory_order_release);
                        }
                        return;
                    }
                }
            }

            Callable* callable;
            size_t baseIndex = 0;
            size_t itemCount = 0;
            std::atomic<bool>* stopRequested = nullptr;
            std::exception_ptr* workerException = nullptr;
            std::mutex* exceptionMutex = nullptr;
        };

        constexpr size_t kMaxTaskSetSize = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
        size_t baseIndex = 0;

        while (baseIndex < itemCount) {
            const size_t currentChunk = (std::min)(kMaxTaskSetSize, itemCount - baseIndex);
            ParallelForTask task(
                &callable,
                baseIndex,
                currentChunk,
                &stopRequested,
                &workerException,
                &exceptionMutex);

            m_scheduler.AddTaskSetToPipe(&task);
            m_scheduler.WaitforTask(&task);

            if (workerException) {
                std::rethrow_exception(workerException);
            }

            baseIndex += currentChunk;
        }
    }

private:
    TaskSchedulerManager() = default;

    enki::TaskScheduler m_scheduler;
    std::vector<uint32_t> m_ioThreadNumbers;
    std::vector<std::unique_ptr<enki::IPinnedTask>> m_ioLoopTasks;
    std::atomic<uint32_t> m_ioRoundRobin = 0;
    bool m_initialized = false;
};

}

using TaskSchedulerManager = br::TaskSchedulerManager;
