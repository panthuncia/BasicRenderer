#pragma once

#include <memory>

#include "Render/MemoryIntrospectionAPI.h"

namespace rg::memory {

std::shared_ptr<IMemorySnapshotProvider> CreateECSMemorySnapshotProvider();

}
