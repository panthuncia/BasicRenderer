#ifndef CLOD_ROOT_CONSTANTS_H
#define CLOD_ROOT_CONSTANTS_H

// Note: indices are aliased for different passes. Aliases must only be reused when
// the corresponding names are not read in the same pass.

#define CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX UintRootConstant2
#define CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX UintRootConstant3

// Histogram pass
#define CLOD_RASTER_BUCKET_HISTOGRAM_COMMAND_DESCRIPTOR_INDEX UintRootConstant4
#define CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX UintRootConstant5

// Prefix sum
#define CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX UintRootConstant6
#define CLOD_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX UintRootConstant7
#define CLOD_RASTER_BUCKETS_SCANNED_BLOCK_SUMS_DESCRIPTOR_INDEX UintRootConstant8
#define CLOD_RASTER_BUCKETS_TOTAL_COUNT_DESCRIPTOR_INDEX UintRootConstant9

// Compaction pass
#define CLOD_RASTER_BUCKETS_WRITE_CURSOR_DESCRIPTOR_INDEX UintRootConstant7 // aliased
#define CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX UintRootConstant8 // aliased
#define CLOD_NUM_RASTER_BUCKETS UintRootConstant9 // aliased
#define CLOD_RASTER_BUCKETS_INDIRECT_ARGS_DESCRIPTOR_INDEX UintRootConstant10
#define CLOD_COMPACTED_APPEND_BASE_COUNTER_DESCRIPTOR_INDEX UintRootConstant11

// Work graph culling pass
#define CLOD_VISIBLE_CLUSTERS_CAPACITY UintRootConstant11 // aliased

// Raster pass
#define CLOD_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX UintRootConstant10 // aliased

// Work graph flags
#define CLOD_WORKGRAPH_TELEMETRY_DESCRIPTOR_INDEX UintRootConstant10 // aliased
#define CLOD_WORKGRAPH_FLAGS UintRootConstant9 // aliased

#define CLOD_WORKGRAPH_FLAG_TELEMETRY_ENABLED (1u << 0)
#define CLOD_WORKGRAPH_FLAG_OCCLUSION_ENABLED (1u << 1)
#define CLOD_WORKGRAPH_FLAG_SW_RASTER_ENABLED (1u << 2)
#define CLOD_WORKGRAPH_FLAG_PHASE2             (1u << 3)
#define CLOD_WORKGRAPH_FLAG_COMPUTE_SW_RASTER  (1u << 4)
#define CLOD_WORKGRAPH_SW_RASTER_THRESHOLD_SHIFT 16 // upper 16 bits of flags = pixel diameter threshold

// Phase 2: descriptor index of Phase 1's HW counter (aliased with histogram command — not used by WG nodes)
#define CLOD_HW_WRITE_BASE_COUNTER_DESCRIPTOR_INDEX UintRootConstant4 // aliased

// Work graph occlusion replay resources (hierarchical culling pass)
#define CLOD_OCCLUSION_REPLAY_BUFFER_DESCRIPTOR_INDEX UintRootConstant5 // aliased
#define CLOD_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX UintRootConstant6 // aliased
#define CLOD_WORKGRAPH_NODE_INPUTS_DESCRIPTOR_INDEX UintRootConstant7 // aliased
#define CLOD_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX UintRootConstant8 // aliased

// Work graph SW raster resources (aliased — only used by work graph nodes)
#define CLOD_SW_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX UintRootConstant1 // aliased (RC0/1 free in WG pass)
#define CLOD_WG_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX UintRootConstant0 // aliased (RC0/1 free in WG pass)

// Sorted->unsorted mapping buffer (compaction pass + raster passes).
// Keep this off RC4 because phase-2 compaction also needs RC4 for the visible-cluster read-base counter.
#define CLOD_SORTED_TO_UNSORTED_MAPPING_DESCRIPTOR_INDEX UintRootConstant0 // aliased with work-graph-only view-raster-info slot

// Shared visible-cluster read mode flags for histogram/compaction passes.
#define CLOD_VISIBLE_CLUSTERS_READ_MODE_FLAGS UintRootConstant12
#define CLOD_VISIBLE_CLUSTERS_READ_FLAG_REVERSED (1u << 0)
#define CLOD_VISIBLE_CLUSTERS_READ_FLAG_BUILD_SW_DISPATCH (1u << 1)
#define CLOD_VISIBLE_CLUSTERS_READ_CAPACITY UintRootConstant13

// Phase 2 SW write base counter for work-graph culling.
#define CLOD_SW_WRITE_BASE_COUNTER_DESCRIPTOR_INDEX UintRootConstant13 // aliased

#endif // CLOD_ROOT_CONSTANTS_H