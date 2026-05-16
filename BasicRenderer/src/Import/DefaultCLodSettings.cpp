#include "Mesh\ClusterLODTypes.h"

ClusterLODBuilderSettings GetDefaultBuilderSettings()
{
    ClusterLODBuilderSettings settings;
    settings.disableSloppyFallback = false;
    settings.lodErrorMergePrevious = 1.5f;
    settings.lodErrorMergeAdditive = 0.0f;
    settings.partitionSizeFloor = 8u;
    settings.preserveImportedNormals = true;
    settings.enableNormalAttributeSimplification = true;
    settings.normalAttributeWeight = 1.0f;
    settings.simplifyTangentWeight = 0.01f;
    settings.simplifyTangentSignWeight = 0.5f;

    settings.enableVoxelFallback = false;
    settings.voxelFallbackMode = ClusterLODVoxelFallbackMode::MeshOnly;
    settings.voxelGridBaseResolution = 32u;
    settings.voxelMinResolution = 0u;
    settings.voxelRaysPerCell = 64u;
    settings.voxelFallbackScalingFactor = 2.f;
    settings.voxelFallbackMaxRetryCount = 10u;
    settings.voxelFallbackGrowthFactor = 1.1f;
    settings.voxelFallbackAcceptanceBias = 1.0f;
    settings.voxelFallbackOpacityThreshold = 0.0f;
    settings.voxelFallbackCarryZeroCoverage = false;
    settings.voxelFallbackPruningMode = ClusterLODVoxelPruningMode::Coverage;

    return settings;
}
