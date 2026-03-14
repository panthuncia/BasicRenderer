#include "Managers/Singletons/TaskSchedulerManager.h"

#include <array>

#include <tbb/global_control.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>
#include <tracy/TracyC.h>

#include <spdlog/spdlog.h>

#include "Telemetry/FrameTaskGraphTelemetry.h"

namespace br {

struct TaskSchedulerManager::RuntimeState {
    std::unique_ptr<tbb::global_control> parallelismControl;
    std::unique_ptr<tbb::task_arena> workerArena;
};

namespace {
constexpr bool kEnableFineGrainedSchedulerTracing = true;

thread_local bool g_isIoWorkerThread = false;
thread_local bool g_isBackgroundWorkerThread = false;

void PlotIoQueueDepth(size_t depth) {
    if constexpr (kEnableFineGrainedSchedulerTracing) {
        TracyCPlotI("TaskScheduler/IO Queue Depth", static_cast<int64_t>(depth));
    }
}

void PlotBackgroundQueueDepth(size_t depth) {
    if constexpr (kEnableFineGrainedSchedulerTracing) {
        TracyCPlotI("TaskScheduler/Background Queue Depth", static_cast<int64_t>(depth));
    }
}

void RecordTaskNodeForTelemetry(
    std::string_view taskName,
    telemetry::CpuTaskDomain domain,
    const std::chrono::steady_clock::time_point& taskStart,
    const std::chrono::steady_clock::time_point& taskEnd) {
    if (taskName.empty()) {
        return;
    }

    std::array<char, 64> telemetryName{};
    const size_t copyLength = (std::min)(telemetryName.size() - 1, taskName.size());
    std::memcpy(telemetryName.data(), taskName.data(), copyLength);
    telemetryName[copyLength] = '\0';
    telemetry::RecordFrameTaskNode(telemetryName.data(), domain, -1, taskStart, taskEnd);
}

}

TaskSchedulerManager& TaskSchedulerManager::GetInstance() {
    static TaskSchedulerManager instance;
    return instance;
}

void TaskSchedulerManager::Initialize(uint32_t ioThreadCount, uint32_t externalTaskThreads, uint32_t backgroundThreadCount) {
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
    m_backgroundShutdownRequested.store(false, std::memory_order_relaxed);
    m_ioTasks.clear();
    m_backgroundTasks.clear();
    m_ioThreads.clear();
    m_backgroundThreads.clear();
    m_ioThreads.reserve(ioThreadCount);
    m_backgroundThreads.reserve(backgroundThreadCount);
    for (uint32_t ioThreadIndex = 0; ioThreadIndex < ioThreadCount; ++ioThreadIndex) {
        m_ioThreads.emplace_back([this]() {
            IoWorkerLoop();
        });
    }
    for (uint32_t backgroundThreadIndex = 0; backgroundThreadIndex < backgroundThreadCount; ++backgroundThreadIndex) {
        m_backgroundThreads.emplace_back([this]() {
            BackgroundWorkerLoop();
        });
    }

    if (externalTaskThreads != 0) {
        spdlog::info(
            "TaskSchedulerManager: externalTaskThreads={} is currently ignored by the oneTBB backend",
            externalTaskThreads);
    }

    m_initialized = true;

    spdlog::info(
        "TaskSchedulerManager initialized: workerThreads={}, ioThreads={}, backgroundThreads={}, externalTaskThreads={}",
        m_workerThreadCount,
        static_cast<uint32_t>(m_ioThreads.size()),
        static_cast<uint32_t>(m_backgroundThreads.size()),
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
        const auto taskStart = std::chrono::steady_clock::now();
        if constexpr (kEnableFineGrainedSchedulerTracing) {
            TracyCZone(ctx, 1);
            TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
            task();
            TracyCZoneEnd(ctx);
        }
        else {
            task();
        }
        RecordTaskNodeForTelemetry(taskNameStorage, telemetry::CpuTaskDomain::IOService, taskStart, std::chrono::steady_clock::now());
        return;
    }

    if (g_isIoWorkerThread) {
        const auto taskStart = std::chrono::steady_clock::now();
        if constexpr (kEnableFineGrainedSchedulerTracing) {
            TracyCZone(ctx, 1);
            TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
            task();
            TracyCZoneEnd(ctx);
        }
        else {
            task();
        }
        RecordTaskNodeForTelemetry(taskNameStorage, telemetry::CpuTaskDomain::IOService, taskStart, std::chrono::steady_clock::now());
        return;
    }

    auto completion = std::make_shared<std::promise<void>>();
    auto future = completion->get_future();

    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        m_ioTasks.emplace_back([task = std::move(task), completion, taskNameStorage]() mutable {
            const auto taskStart = std::chrono::steady_clock::now();
            if constexpr (kEnableFineGrainedSchedulerTracing) {
                TracyCZone(ctx, 1);
                TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
                try {
                    task();
                    completion->set_value();
                }
                catch (...) {
                    completion->set_exception(std::current_exception());
                }
                TracyCZoneEnd(ctx);
            }
            else {
                try {
                    task();
                    completion->set_value();
                }
                catch (...) {
                    completion->set_exception(std::current_exception());
                }
            }
            RecordTaskNodeForTelemetry(taskNameStorage, telemetry::CpuTaskDomain::IOService, taskStart, std::chrono::steady_clock::now());
        });
        PlotIoQueueDepth(m_ioTasks.size());
    }

    m_ioCv.notify_one();
    future.get();
}

void TaskSchedulerManager::RunBackgroundTask(std::function<void()>&& task) {
    RunBackgroundTask({}, std::move(task));
}

void TaskSchedulerManager::RunBackgroundTask(std::string_view taskName, std::function<void()>&& task) {
    const std::string taskNameStorage = taskName.empty()
        ? std::string("TaskScheduler::RunBackgroundTask")
        : std::string(taskName);

    if (!m_initialized || m_backgroundThreads.empty()) {
        const auto taskStart = std::chrono::steady_clock::now();
        if constexpr (kEnableFineGrainedSchedulerTracing) {
            TracyCZone(ctx, 1);
            TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
            task();
            TracyCZoneEnd(ctx);
        }
        else {
            task();
        }
        RecordTaskNodeForTelemetry(taskNameStorage, telemetry::CpuTaskDomain::BackgroundService, taskStart, std::chrono::steady_clock::now());
        return;
    }

    if (g_isBackgroundWorkerThread) {
        const auto taskStart = std::chrono::steady_clock::now();
        if constexpr (kEnableFineGrainedSchedulerTracing) {
            TracyCZone(ctx, 1);
            TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
            task();
            TracyCZoneEnd(ctx);
        }
        else {
            task();
        }
        RecordTaskNodeForTelemetry(taskNameStorage, telemetry::CpuTaskDomain::BackgroundService, taskStart, std::chrono::steady_clock::now());
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_backgroundMutex);
        m_backgroundTasks.emplace_back([task = std::move(task), taskNameStorage]() mutable {
            const auto taskStart = std::chrono::steady_clock::now();
            if constexpr (kEnableFineGrainedSchedulerTracing) {
                TracyCZone(ctx, 1);
                TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
                task();
                TracyCZoneEnd(ctx);
            }
            else {
                task();
            }
            RecordTaskNodeForTelemetry(taskNameStorage, telemetry::CpuTaskDomain::BackgroundService, taskStart, std::chrono::steady_clock::now());
        });
        PlotBackgroundQueueDepth(m_backgroundTasks.size());
    }

    m_backgroundCv.notify_one();
}

void TaskSchedulerManager::Cleanup() {
    if (!m_initialized) {
        return;
    }

    m_ioShutdownRequested.store(true, std::memory_order_release);
    m_backgroundShutdownRequested.store(true, std::memory_order_release);
    m_ioCv.notify_all();
    m_backgroundCv.notify_all();
    for (std::thread& ioThread : m_ioThreads) {
        if (ioThread.joinable()) {
            ioThread.join();
        }
    }
    for (std::thread& backgroundThread : m_backgroundThreads) {
        if (backgroundThread.joinable()) {
            backgroundThread.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        m_ioTasks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_backgroundMutex);
        m_backgroundTasks.clear();
    }

    m_ioThreads.clear();
    m_backgroundThreads.clear();
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

    if (!m_initialized || itemCount == 1 || !m_runtimeState || !m_runtimeState->workerArena) {
        thread_local bool s_namedInlineThread = false;
        const auto taskStart = std::chrono::steady_clock::now();
        if constexpr (kEnableFineGrainedSchedulerTracing) {
            if (!s_namedInlineThread) {
                TracyCSetThreadName("Task Inline");
                s_namedInlineThread = true;
            }

            TracyCZone(ctx, 1);
            TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
            TracyCZoneValue(ctx, itemCount);
            for (size_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
                func(itemIndex);
            }
            TracyCZoneEnd(ctx);
        }
        else {
            for (size_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
                func(itemIndex);
            }
        }
        RecordTaskNodeForTelemetry(taskNameStorage, telemetry::CpuTaskDomain::Worker, taskStart, std::chrono::steady_clock::now());
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
                thread_local bool s_namedWorkerThread = false;
                const auto taskStart = std::chrono::steady_clock::now();
                if constexpr (kEnableFineGrainedSchedulerTracing) {
                    if (!s_namedWorkerThread) {
                        TracyCSetThreadName("Task Worker");
                        s_namedWorkerThread = true;
                    }

                    TracyCZone(ctx, 1);
                    TracyCZoneName(ctx, taskNameStorage.c_str(), taskNameStorage.size());
                    TracyCZoneValue(ctx, chunkEnd - chunkBegin);
                    for (size_t itemIndex = chunkBegin; itemIndex < chunkEnd; ++itemIndex) {
                        func(itemIndex);
                    }
                    TracyCZoneEnd(ctx);
                }
                else {
                    for (size_t itemIndex = chunkBegin; itemIndex < chunkEnd; ++itemIndex) {
                        func(itemIndex);
                    }
                }
                RecordTaskNodeForTelemetry(taskNameStorage, telemetry::CpuTaskDomain::Worker, taskStart, std::chrono::steady_clock::now());
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

void TaskSchedulerManager::BackgroundWorkerLoop() {
    g_isBackgroundWorkerThread = true;

    static std::atomic<uint32_t> s_workerIdCounter = 0;
    const uint32_t workerId = s_workerIdCounter.fetch_add(1, std::memory_order_relaxed);
    std::array<char, 32> workerName{};
    const int charsWritten = std::snprintf(workerName.data(), workerName.size(), "Background Worker %u", workerId);
    if constexpr (kEnableFineGrainedSchedulerTracing) {
        if (charsWritten > 0) {
            TracyCSetThreadName(workerName.data());
        }
    }

    while (!m_backgroundShutdownRequested.load(std::memory_order_acquire)) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_backgroundMutex);
            m_backgroundCv.wait(lock, [this]() {
                return m_backgroundShutdownRequested.load(std::memory_order_relaxed) || !m_backgroundTasks.empty();
            });

            if (m_backgroundShutdownRequested.load(std::memory_order_relaxed) && m_backgroundTasks.empty()) {
                break;
            }

            task = std::move(m_backgroundTasks.front());
            m_backgroundTasks.pop_front();
            PlotBackgroundQueueDepth(m_backgroundTasks.size());
        }

        task();
    }

    g_isBackgroundWorkerThread = false;
}

}
