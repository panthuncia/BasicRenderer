#pragma once

#include <cstdint>

#include "Render/GraphExtensions/ClusterLOD/ReyesTessellationTable.h"

ReyesTessellationTableData BuildGeneratedReyesTessellationTableData(uint32_t maxEdgeSegments, uint32_t lookupSize);