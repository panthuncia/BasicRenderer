#include "Render/GraphExtensions/ClusterLOD/ReyesTessellationReferenceSource.h"

#include "../../../../../vk_tessellated_clusters/thirdparty/epicgames_tessellation/tessellation_table_epicgames_raw.hpp"

const ReyesPackedTessellationTableSource& GetReferenceReyesPackedTessellationTableSource()
{
    static const ReyesPackedTessellationTableSource source{
        tessellation_table::max_edge_segments,
        tessellation_table::max_vertices,
        tessellation_table::max_configs,
        tessellation_table::vertices,
        tessellation_table::triangles,
        tessellation_table::configs,
    };

    return source;
}