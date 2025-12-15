#pragma once
#include <cstdint>

enum class FeatureInfoStructType : uint64_t {
    AdapterInfo = 1,
    Architecture = 2,
    Features = 3,
    MeshShaders = 4,
    RayTracing = 5,
    ShadingRate = 6,
    EnhancedBarriers = 7,
	ResourceAllocation = 8,
};

struct FeatureInfoHeader {
    FeatureInfoStructType sType{};
    FeatureInfoHeader* pNext = nullptr;

    // Caller sets these:
    uint32_t structSize = 0;
    uint32_t structVersion = 1;
};

enum class ShaderModel : uint16_t {
    Unknown = 0,
	SM_6_0, SM_6_1, SM_6_2, SM_6_3, SM_6_4, SM_6_5, SM_6_6, SM_6_7, SM_6_8, SM_6_9,
};

// "quality level" enums that are RHI-defined (NOT necessarily DX tiers).
enum class MeshShaderLevel : uint8_t { None, Mesh, MeshPlusTask };
enum class RayTracingLevel : uint8_t { None, Pipeline, PipelinePlusQuery };
enum class ShadingRateLevel : uint8_t { None, PerDraw, Attachment };

// ---------------- Caps structs ----------------

struct AdapterFeatureInfo {
    FeatureInfoHeader header{ FeatureInfoStructType::AdapterInfo, nullptr, sizeof(AdapterFeatureInfo), 1 };

    char     name[128]{};
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;

    uint64_t dedicatedVideoMemory = 0;
    uint64_t dedicatedSystemMemory = 0;
    uint64_t sharedSystemMemory = 0;
};

struct ArchitectureFeatureInfo {
    FeatureInfoHeader header{ FeatureInfoStructType::Architecture, nullptr, sizeof(ArchitectureFeatureInfo), 1 };

    bool uma = false;
    bool cacheCoherentUMA = false;
    bool isolatedMMU = false;
};

struct ShaderFeatureInfo {
    FeatureInfoHeader header{ FeatureInfoStructType::Features, nullptr, sizeof(ShaderFeatureInfo), 1 };

    // "How modern is the shader compiler/runtime path?"
    ShaderModel maxShaderModel = ShaderModel::Unknown;

    // (DX12 backend derives these from ResourceBindingTier/ResourceHeapTier internally.)
    bool unifiedResourceHeaps = false;       // "heap tier 2"-like: heaps can back any resource types
    bool unboundedDescriptorTables = false;  // "binding tier 3"-like: very loose binding limits / unbounded tables

    // Shader capabilities
    bool waveOps = false;
    bool int64ShaderOps = false;
    bool barycentrics = false;
    bool derivativesInMeshAndTaskShaders = false;
    bool atomicInt64OnGroupShared = false;
    bool atomicInt64OnTypedResource = false;
    bool atomicInt64OnDescriptorHeapResources = false;
};

struct MeshShaderFeatureInfo {
    FeatureInfoHeader header{ FeatureInfoStructType::MeshShaders, nullptr, sizeof(MeshShaderFeatureInfo), 1 };

    bool meshShader = false;     // supports mesh shaders
    bool taskShader = false;     // supports task/amplification shaders
    bool derivatives = false;    // derivatives in mesh/task

    [[nodiscard]] constexpr MeshShaderLevel Level() const noexcept {
        if (!meshShader) return MeshShaderLevel::None;
        return taskShader ? MeshShaderLevel::MeshPlusTask : MeshShaderLevel::Mesh;
    }
};

struct RayTracingFeatureInfo {
    FeatureInfoHeader header{ FeatureInfoStructType::RayTracing, nullptr, sizeof(RayTracingFeatureInfo), 1 };

    bool pipeline = false;   // RT pipeline + shader tables (DXR pipeline / VK ray_tracing_pipeline)
    bool rayQuery = false;   // inline ray queries (DXR 1.1-ish / VK ray_query)
    bool indirect = false;   // indirect trace support

    [[nodiscard]] constexpr RayTracingLevel Level() const noexcept {
        if (!pipeline) return RayTracingLevel::None;
        return rayQuery ? RayTracingLevel::PipelinePlusQuery : RayTracingLevel::Pipeline;
    }
};

struct ShadingRateFeatureInfo {
    FeatureInfoHeader header{ FeatureInfoStructType::ShadingRate, nullptr, sizeof(ShadingRateFeatureInfo), 1 };

    bool perDrawRate = false;          // choose rate per draw / pipeline
    bool attachmentRate = false;       // shading-rate image / attachment
    bool perPrimitiveRate = false;     // per-primitive shading rate (if supported)
    bool additionalRates = false;      // extra shading rates beyond the core set (DX12 has a bit)

    uint32_t tileSize = 0;             // only meaningful if attachmentRate == true

    constexpr ShadingRateLevel Level() const noexcept {
        if (attachmentRate) return ShadingRateLevel::Attachment;
        if (perDrawRate)    return ShadingRateLevel::PerDraw;
        return ShadingRateLevel::None;
    }
};

struct EnhancedBarriersFeatureInfo {
    FeatureInfoHeader header{ FeatureInfoStructType::EnhancedBarriers, nullptr, sizeof(EnhancedBarriersFeatureInfo), 1 };

    bool enhancedBarriers = false;
    bool relaxedFormatCasting = false;
};

struct ResourceAllocationFeatureInfo {
    FeatureInfoHeader header{ FeatureInfoStructType::ResourceAllocation, nullptr, sizeof(ResourceAllocationFeatureInfo), 1 };

    bool gpuUploadHeapSupported = false;     // D3D12_OPTIONS16
    bool tightAlignmentSupported = false;    // D3D12_TIGHT_ALIGNMENT tier >= 1

    bool createNotZeroedHeapSupported = false; // "heap flag create_not_zeroed" proxy (D3D12MA-style)
};