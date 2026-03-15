#include "Render/TbbTaskService.h"
#include "Managers/Singletons/TaskSchedulerManager.h"

namespace br {

void TbbTaskService::ParallelFor(std::string_view taskName, size_t itemCount, std::function<void(size_t)> func) {
    TaskSchedulerManager::GetInstance().ParallelFor(taskName, itemCount, std::move(func));
}

}
