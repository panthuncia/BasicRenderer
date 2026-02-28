#include "Managers/Singletons/TaskSchedulerManager.h"

#include <spdlog/spdlog.h>

namespace br {

namespace {

class IoPinnedLoopTask final : public enki::IPinnedTask {
public:
    explicit IoPinnedLoopTask(enki::TaskScheduler& scheduler)
        : m_scheduler(scheduler) {
    }

    void Execute() override {
        while (!m_scheduler.GetIsShutdownRequested()) {
            m_scheduler.WaitForNewPinnedTasks();
            m_scheduler.RunPinnedTasks();
        }
    }

private:
    enki::TaskScheduler& m_scheduler;
};

class OneShotPinnedTask final : public enki::IPinnedTask {
public:
    OneShotPinnedTask(std::function<void()>&& task, uint32_t pinnedThread)
        : m_task(std::move(task)) {
        threadNum = pinnedThread;
    }

    void Execute() override {
        try {
            m_task();
        }
        catch (...) {
            m_exception = std::current_exception();
        }
    }

    std::exception_ptr GetException() const {
        return m_exception;
    }

private:
    std::function<void()> m_task;
    std::exception_ptr m_exception;
};

}

TaskSchedulerManager& TaskSchedulerManager::GetInstance() {
    static TaskSchedulerManager instance;
    return instance;
}

void TaskSchedulerManager::Initialize(uint32_t ioThreadCount, uint32_t externalTaskThreads) {
    if (m_initialized) {
        return;
    }

    enki::TaskSchedulerConfig config;
    config.numTaskThreadsToCreate += ioThreadCount;
    config.numExternalTaskThreads = externalTaskThreads;

    m_scheduler.Initialize(config);
    m_initialized = true;

    const uint32_t totalTaskThreads = m_scheduler.GetNumTaskThreads();
    const uint32_t availablePinnedWorkerThreads = totalTaskThreads > 0 ? (totalTaskThreads - 1u) : 0u;
    const uint32_t actualIoThreadCount = (std::min)(ioThreadCount, availablePinnedWorkerThreads);

    m_ioThreadNumbers.clear();
    m_ioLoopTasks.clear();
    m_ioThreadNumbers.reserve(actualIoThreadCount);
    m_ioLoopTasks.reserve(actualIoThreadCount);

    for (uint32_t ioThreadIndex = 0; ioThreadIndex < actualIoThreadCount; ++ioThreadIndex) {
        const uint32_t pinnedThreadNum = totalTaskThreads - 1u - ioThreadIndex;
        auto ioLoopTask = std::make_unique<IoPinnedLoopTask>(m_scheduler);
        ioLoopTask->threadNum = pinnedThreadNum;
        m_scheduler.AddPinnedTask(ioLoopTask.get());
        m_ioThreadNumbers.push_back(pinnedThreadNum);
        m_ioLoopTasks.push_back(std::move(ioLoopTask));
    }

    spdlog::info(
        "TaskSchedulerManager initialized: taskThreads={}, ioThreads={}, externalTaskThreads={}",
        m_scheduler.GetNumTaskThreads(),
        actualIoThreadCount,
        externalTaskThreads);
}

void TaskSchedulerManager::RunIoTask(std::function<void()>&& task) {
    if (!m_initialized || m_ioThreadNumbers.empty()) {
        task();
        return;
    }

    const uint32_t ioLaneIndex = m_ioRoundRobin.fetch_add(1, std::memory_order_relaxed) % static_cast<uint32_t>(m_ioThreadNumbers.size());
    OneShotPinnedTask ioTask(std::move(task), m_ioThreadNumbers[ioLaneIndex]);
    m_scheduler.AddPinnedTask(&ioTask);
    m_scheduler.WaitforTask(&ioTask);

    if (const std::exception_ptr ioException = ioTask.GetException()) {
        std::rethrow_exception(ioException);
    }
}

void TaskSchedulerManager::Cleanup() {
    if (!m_initialized) {
        return;
    }

    m_scheduler.WaitforAllAndShutdown();
    m_ioLoopTasks.clear();
    m_ioThreadNumbers.clear();
    m_ioRoundRobin.store(0, std::memory_order_relaxed);
    m_initialized = false;
    spdlog::info("TaskSchedulerManager shutdown complete");
}

}
