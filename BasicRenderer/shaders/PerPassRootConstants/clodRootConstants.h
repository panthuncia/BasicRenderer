#ifndef CLOD_ROOT_CONSTANTS_H
#define CLOD_ROOT_CONSTANTS_H

// Note: indices are aliased for different passes. Could put in a separate buffer.
// 0 and 1 used by indirect command signature

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

// Raster pass
#define CLOD_VIEW_UAV_INDICES_BUFFER_DESCRIPTOR_INDEX UintRootConstant10 // aliased

#endif // CLOD_ROOT_CONSTANTS_H