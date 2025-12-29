#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Interfaces/IResourceProvider.h"
#include "Resources/ResourceIdentifier.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "ShaderBuffers.h"

class Skeleton; // base skeleton asset or instance
class BufferView;

class SkeletonManager : public IResourceProvider {
public:
    static std::unique_ptr<SkeletonManager> CreateUnique() {
        return std::unique_ptr<SkeletonManager>(new SkeletonManager());
    }

    // Called when a renderable becomes active/inactive and references a skinning instance.
    // Multiple renderables may call Acquire/Release for the same instance.
    uint32_t AcquireSkinningInstance(const std::shared_ptr<Skeleton>& skinningInstance);
    void     ReleaseSkinningInstance(Skeleton* skinningInstance);

    // Tick animations for all active skeletons
    void TickAnimations(float elapsedSeconds);

    // Upload pose for a specific instance (or call UpdateAllDirtyInstances once per frame).
    void UpdateInstanceTransforms(Skeleton& skinningInstance);
    void UpdateAllDirtyInstances();

    // Useful?
    // void SetAnimation(Skeleton& inst);

    // IResourceProvider
    std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
    std::vector<ResourceIdentifier> GetSupportedKeys() override;

private:
    SkeletonManager();

    struct BaseRecord {
        std::unique_ptr<BufferView> invBindView;
        uint32_t boneCount = 0;
        uint32_t refCount = 0;
        // cached matrix offset (index, not bytes)
        uint32_t invBindOffsetMatrices = 0;
    };

    struct InstanceRecord {
        std::unique_ptr<BufferView> transformsView;
        uint32_t boneCount = 0;
        uint32_t refCount = 0;

        uint32_t instanceSlot = 0xFFFFFFFF;
        bool dirty = true;

        const Skeleton* base = nullptr;

        uint32_t transformOffsetMatrices = 0;
        uint32_t invBindOffsetMatrices = 0;
    };

private:
    // Global packed buffers
    std::shared_ptr<DynamicBuffer> m_inverseBindMatrices;  // float4x4[]
    std::shared_ptr<DynamicBuffer> m_boneTransforms;       // float4x4[]
    std::shared_ptr<DynamicStructuredBuffer<SkinningInstanceGPUInfo>> m_instanceInfo; // slot -> offsets/count

    // Resource provider map
    std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;

    // Records
    std::unordered_map<const Skeleton*, BaseRecord>    m_bases;
    std::unordered_map<const Skeleton*, InstanceRecord> m_instances;

    // Free-list for instance slots
    std::vector<uint32_t> m_freeInstanceSlots;
    uint32_t m_slotsUsed = 0;

private:
    BaseRecord& AcquireBase(const std::shared_ptr<Skeleton>& baseSkeleton);
    void           ReleaseBase(const Skeleton* baseSkeleton);

    uint32_t       AllocateInstanceSlot();
    void           FreeInstanceSlot(uint32_t slot);

    static constexpr uint32_t kInvalidSlot = 0xFFFFFFFF;
};
