#include "Managers/SkeletonManager.h"

#include "Animation/Skeleton.h"
#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/UploadManager.h"
#include "../../generated/BuiltinResources.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"

#include <DirectXMath.h>

static uint32_t BytesToMatrixIndex(size_t byteOffset) {
    return static_cast<uint32_t>(byteOffset / sizeof(DirectX::XMMATRIX));
}

SkeletonManager::SkeletonManager() {
    m_inverseBindMatrices = DynamicBuffer::CreateShared(sizeof(DirectX::XMMATRIX), 1, "InverseBindMatricesPacked");
    m_boneTransforms = DynamicBuffer::CreateShared(sizeof(DirectX::XMMATRIX), 1, "BoneTransformsPacked");

    m_instanceInfo = DynamicStructuredBuffer<SkinningInstanceGPUInfo>::CreateShared(64, "SkinningInstanceInfo");

    // Expose via resource provider keys (replace with your Builtin identifiers)
    m_resources[Builtin::SkeletonResources::InverseBindMatrices] = m_inverseBindMatrices;
    m_resources[Builtin::SkeletonResources::BoneTransforms] = m_boneTransforms;
    m_resources[Builtin::SkeletonResources::SkinningInstanceInfo] = m_instanceInfo;
}

SkeletonManager::BaseRecord& SkeletonManager::AcquireBase(const std::shared_ptr<Skeleton>& baseSkeleton) {
    auto it = m_bases.find(baseSkeleton.get());
    if (it != m_bases.end()) {
        it->second.refCount++;
        return it->second;
    }

    BaseRecord rec;
    rec.boneCount = static_cast<uint32_t>(baseSkeleton->m_bones.size());
    rec.refCount = 1;

    // Allocate + upload inverse binds once
    const size_t bytes = rec.boneCount * sizeof(DirectX::XMMATRIX);
    rec.invBindView = m_inverseBindMatrices->AddData(baseSkeleton->m_inverseBindMatrices.data(), bytes, sizeof(DirectX::XMMATRIX));
    rec.invBindOffsetMatrices = BytesToMatrixIndex(rec.invBindView->GetOffset());

    auto [insIt, _ok] = m_bases.emplace(baseSkeleton.get(), std::move(rec));
    return insIt->second;
}

void SkeletonManager::ReleaseBase(const Skeleton* baseSkeleton) {
    auto it = m_bases.find(baseSkeleton);
    if (it == m_bases.end())
        return;

    auto& rec = it->second;
    if (--rec.refCount > 0)
        return;

    if (rec.invBindView)
        m_inverseBindMatrices->Deallocate(rec.invBindView.get());

    m_bases.erase(it);
}

uint32_t SkeletonManager::AllocateInstanceSlot() {
    if (!m_freeInstanceSlots.empty()) {
        uint32_t slot = m_freeInstanceSlots.back();
        m_freeInstanceSlots.pop_back();
        return slot;
    }
    return m_slotsUsed++; // grows as needed
}

void SkeletonManager::FreeInstanceSlot(uint32_t slot) {
    if (slot != kInvalidSlot)
        m_freeInstanceSlots.push_back(slot);
}

uint32_t SkeletonManager::AcquireSkinningInstance(const std::shared_ptr<Skeleton>& skinningInstance) {
    // Expect: skinningInstance is NOT a base skeleton.
    // It should reference a base skeleton (see "Skeleton type changes" section below).
    auto it = m_instances.find(skinningInstance.get());
    if (it != m_instances.end()) {
        it->second.refCount++;
        return it->second.instanceSlot;
    }

    // Identify base skeleton
    auto baseShared = skinningInstance->GetBaseSkeletonShared();
    auto& baseRec = AcquireBase(baseShared);

    InstanceRecord rec;
    rec.base = baseShared.get();
    rec.boneCount = baseRec.boneCount;
    rec.refCount = 1;
    rec.dirty = true;

    // Allocate transforms region (unique per skinning instance)
    const size_t bytes = rec.boneCount * sizeof(DirectX::XMMATRIX);
    rec.transformsView = m_boneTransforms->Allocate(bytes, sizeof(DirectX::XMMATRIX));
    rec.transformOffsetMatrices = BytesToMatrixIndex(rec.transformsView->GetOffset());
    rec.invBindOffsetMatrices = baseRec.invBindOffsetMatrices;

    // Allocate instance slot and write GPU info
    rec.instanceSlot = AllocateInstanceSlot();
    SkinningInstanceGPUInfo info;
    info.transformOffsetMatrices = rec.transformOffsetMatrices;
    info.invBindOffsetMatrices = rec.invBindOffsetMatrices;
    info.boneCount = rec.boneCount;

    m_instanceInfo->UpdateAt(rec.instanceSlot, info);

    // Store slot on the instance so renderables can grab it without querying manager
    skinningInstance->SetSkinningInstanceSlot(rec.instanceSlot);

    auto [insIt, _ok] = m_instances.emplace(skinningInstance.get(), std::move(rec));
    return insIt->second.instanceSlot;
}

void SkeletonManager::ReleaseSkinningInstance(Skeleton* skinningInstance) {
    auto it = m_instances.find(skinningInstance);
    if (it == m_instances.end())
        return;

    auto& rec = it->second;
    if (--rec.refCount > 0)
        return;

    if (rec.transformsView)
        m_boneTransforms->Deallocate(rec.transformsView.get());

    FreeInstanceSlot(rec.instanceSlot);

    // Decrement base usage
    ReleaseBase(rec.base);

    // Clear instance's slot so stale data can't be used accidentally
    skinningInstance->SetSkinningInstanceSlot(kInvalidSlot);

    m_instances.erase(it);
}

void SkeletonManager::UpdateInstanceTransforms(Skeleton& inst) {
    auto it = m_instances.find(&inst);
    if (it == m_instances.end())
        return;

    auto& rec = it->second;
    if (!rec.transformsView)
        return;

    // Gather matrices like your old Skeleton::UpdateTransforms()
    // (You can keep this CPU data inside Skeleton and just expose a pointer/span too.)
    inst.GatherBoneMatricesToCPUBuffer(); // <--- or inline the gather here

    const size_t bytes = rec.boneCount * sizeof(DirectX::XMMATRIX);
    BUFFER_UPLOAD(inst.GetCPUBoneMatrices(), bytes,
        UploadManager::UploadTarget::FromShared(m_boneTransforms),
        rec.transformsView->GetOffset());

    //rec.dirty = false;
}

void SkeletonManager::UpdateAllDirtyInstances() {
    for (auto& [ptr, rec] : m_instances) {
        if (rec.dirty) {
            UpdateInstanceTransforms(*const_cast<Skeleton*>(ptr));
        }
    }
}

std::shared_ptr<Resource> SkeletonManager::ProvideResource(ResourceIdentifier const& key) {
    return m_resources[key];
}

std::vector<ResourceIdentifier> SkeletonManager::GetSupportedKeys() {
    std::vector<ResourceIdentifier> keys;
    keys.reserve(m_resources.size());
    for (auto const& [key, _] : m_resources) keys.push_back(key);
    return keys;
}
