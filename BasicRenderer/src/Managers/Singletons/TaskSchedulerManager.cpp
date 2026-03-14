#include "Managers/Singletons/TaskSchedulerManager.h"

#include <array>

#include <tbb/global_control.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>
#include <tracy/TracyC.h>

#include <spdlog/spdlog.h>

namespace br {

struct TaskSchedulerManager::RuntimeState {
    std::unique_ptr<tbb::global_control> parallelismControl;
    std::unique_ptr<tbb::task_arena> workerArena;
};

namespace {
constexpr bool kEnableFineGrainedSchedulerTracing = false;

thread_local bool g_isIoWorkerThread = false;

void PlotIoQueueDepth(size_t depth) {
    if constexpr (kEnableFineGrainedSchedulerTracing) {
        TracyCPlotI("TaskScheduler/IO Queue Depth", static_cast<int64_t>(depth));
    }
}

}

TaskSchedulerManager& TaskSchedulerManager::GetInstance() {
    static TaskSchedulerManager instance;
    return instance;
}

void TaskSchedulerManager::Initialize(uint32_t ioThreadCount, uint32_t externalTaskThreads) {
    if (m_initialized) {
        return;
    }

    const uint32_t detectedConcurrency = (std::max)(std::thread::hardware_concurrency(), 1u);
    m_workerThreadCount = detectedConcurrency;
    m_runtimeState = std::make_unique<RuntimeState>();
    m_runtimeState->parallelismControl = std::make_unique<tbb::global_control>(
        tbb::global_control::max_allowed_parallelism,
        static_cast<size_t>(m_workerThreadCount));
    m_runtimeState->workerArena = std::make_unique<tbb::task_arena>(static_cast<int>(m_workerThreadCount));

    m_ioShutdownRequested.store(false, std::memory_order_relaxed);
    m_ioTasks.clear();
    m_ioThreads.clear();
    m_ioThreads.reserve(ioThreadCount);
    for (uint32_t ioThreadIndex = 0; ioThreadIndex < ioThreadCount; ++ioThreadIndex) {
        m_ioThreads.emplace_back([this]() {
            IoWorkerLoop();
        });
    }

    if (externalTaskThreads != 0) {
        spdlog::info(
            "TaskSchedulerManager: externalTaskThreads={} is currently ignored by the oneTBB backend",
            externalTaskThreads);
    }

    m_initialized = true;

    spdlog::info(
        "TaskSchedulerManager initialized: workerThreads={}, ioThreads={}, externalTaskThreads={}",
        m_workerThreadCount,
        static_cast<uint32_t>(m_ioThreads.size()),
        externalTaskThreads);
}

void TaskSchedulerManager::RunIoTask(std::function<void()>&& task) {
    RunIoTask({}, std::move(task));
}

void TaskSchedulerManager::RunIoTask(std::string_view taskName, std::function<void()>&& task) {
    const std::string taskNameStorage = taskName.empty()
        ? std::string("TaskScheduler::RunIoTask")
        : std::string(taskName);

    if (!m_initialized || m_ioThreads.empty()) {
        if constexpr (kEnableFineGrainedSchedulerTracing) {
            TracyCZone(ctx, 1);
            TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
            task();
            TracyCZoneEnd(ctx);
            return;
        }
        task();
        return;
    }

    if (g_isIoWorkerThread) {
        if constexpr (kEnableFineGrainedSchedulerTracing) {
            TracyCZone(ctx, 1);
            TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
            task();
            TracyCZoneEnd(ctx);
            return;
        }
        task();
        return;
    }

    auto completion = std::make_shared<std::promise<void>>();
    auto future = completion->get_future();

    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        m_ioTasks.emplace_back([task = std::move(task), completion, taskNameStorage]() mutable {
            (void)taskNameStorage;
            try {
                task();
                completion->set_value();
            }
            catch (...) {
                completion->set_exception(std::current_exception());
            }
        });
        PlotIoQueueDepth(m_ioTasks.size());
    }

    m_ioCv.notify_one();
    future.get();
}

void TaskSchedulerManager::Cleanup() {
    if (!m_initialized) {
        return;
    }

    m_ioShutdownRequested.store(true, std::memory_order_release);
    m_ioCv.notify_all();
    for (std::thread& ioThread : m_ioThreads) {
        if (ioThread.joinable()) {
            ioThread.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        m_ioTasks.clear();
    }

    m_ioThreads.clear();
    m_runtimeState.reset();
    m_ioRoundRobin.store(0, std::memory_order_relaxed);
    m_workerThreadCount = 0;
    m_initialized = false;
    spdlog::info("TaskSchedulerManager shutdown complete");
}

void TaskSchedulerManager::ParallelForImpl(std::string_view taskName, size_t itemCount, std::function<void(size_t)>&& func) {
    if (itemCount == 0) {
        return;
    }

    const std::string taskNameStorage = taskName.empty()
        ? std::string("TaskScheduler::ParallelFor")
        : std::string(taskName);
    (void)taskNameStorage;

    if (!m_initialized || itemCount == 1 || !m_runtimeState || !m_runtimeState->workerArena) {
        for (size_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
            func(itemIndex);
        }
        return;
    }

    const size_t desiredChunks = (std::max)(size_t{ 1 }, static_cast<size_t>((std::max)(m_workerThreadCount, 1u)) * 4u);
    const size_t chunkCount = (std::min)(itemCount, desiredChunks);
    const size_t chunkSize = (itemCount + chunkCount - 1) / chunkCount;

    m_runtimeState->workerArena->execute([&]() {
        tbb::task_group group;
        for (size_t chunkBegin = 0; chunkBegin < itemCount; chunkBegin += chunkSize) {
            const size_t chunkEnd = (std::min)(itemCount, chunkBegin + chunkSize);
            group.run([&, chunkBegin, chunkEnd]() {
                for (size_t itemIndex = chunkBegin; itemIndex < chunkEnd; ++itemIndex) {
                    func(itemIndex);
                }
            });
        }
        group.wait();
    });
}

void TaskSchedulerManager::IoWorkerLoop() {
    g_isIoWorkerThread = true;

    static std::atomic<uint32_t> s_workerIdCounter = 0;
    const uint32_t workerId = s_workerIdCounter.fetch_add(1, std::memory_order_relaxed);
    std::array<char, 32> workerName{};
    const int charsWritten = std::snprintf(workerName.data(), workerName.size(), "IO Worker %u", workerId);
    if constexpr (kEnableFineGrainedSchedulerTracing) {
        if (charsWritten > 0) {
            TracyCSetThreadName(workerName.data());
        }
    }

    while (!m_ioShutdownRequested.load(std::memory_order_acquire)) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_ioMutex);
            m_ioCv.wait(lock, [this]() {
                return m_ioShutdownRequested.load(std::memory_order_relaxed) || !m_ioTasks.empty();
            });

            if (m_ioShutdownRequested.load(std::memory_order_relaxed) && m_ioTasks.empty()) {
                break;
            }

            task = std::move(m_ioTasks.front());
            m_ioTasks.pop_front();
            PlotIoQueueDepth(m_ioTasks.size());
        }

        task();
    }

    g_isIoWorkerThread = false;
}

}
