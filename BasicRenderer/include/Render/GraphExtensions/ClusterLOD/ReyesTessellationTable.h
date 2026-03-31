#pragma once

#include <cstdint>
#include <vector>

#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"

struct ReyesTessellationTableData
{
    std::vector<uint32_t> vertices;
    std::vector<uint32_t> triangles;
    std::vector<CLodReyesTessTableConfigEntry> configs;
};

const ReyesTessellationTableData& GetReyesTessellationTableData();