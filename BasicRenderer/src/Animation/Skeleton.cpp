#include "Animation/Skeleton.h"

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <algorithm>

#include "Scene/Components.h"

Skeleton::Matrix Skeleton::ComposeTRS_(const Components::Position& p,
    const Components::Rotation& r,
    const Components::Scale& s)
{
    using namespace DirectX;

    XMMATRIX S = XMMatrixScalingFromVector(s.scale);
    XMMATRIX R = XMMatrixRotationQuaternion(r.rot);
    XMMATRIX T = XMMatrixTranslationFromVector(p.pos);

    return XMMatrixMultiply(XMMatrixMultiply(S, R), T);
}

Skeleton::Matrix Skeleton::ComposeTRS_(const Components::Transform& t)
{
    return ComposeTRS_(t.pos, t.rot, t.scale);
}

Skeleton::Skeleton(const std::vector<flecs::entity>& nodes,
    const std::vector<Matrix>& inverseBindMatrices)
{
    m_isBaseSkeleton = true;

    if (nodes.empty()) {
        spdlog::warn("Skeleton: constructed with 0 nodes");
        return;
    }

    if (!inverseBindMatrices.empty() && inverseBindMatrices.size() != nodes.size()) {
        spdlog::warn("Skeleton: inverseBindMatrices size ({}) != nodes size ({})",
            inverseBindMatrices.size(), nodes.size());
    }

    BuildBaseFromNodes_(nodes);

    // Copy inverse binds. SkeletonManager will upload these when base becomes active.
    m_inverseBindMatrices = inverseBindMatrices;

    // Base skeleton could keep an idle pose buffer for debugging,
    // but runtime skinning must use instances.
}

Skeleton::Skeleton(const std::shared_ptr<Skeleton>& baseSkeleton)
{
    if (!baseSkeleton) {
        spdlog::error("Skeleton(instance): baseSkeleton is null");
        return;
    }
    if (!baseSkeleton->IsBaseSkeleton()) {
        // Allow instance-from-instance by taking its base.
        m_baseSkeleton = baseSkeleton->GetBaseSkeletonShared();
    }
    else {
        m_baseSkeleton = baseSkeleton;
    }

    m_isBaseSkeleton = false;

    EnsureInstanceBuffersSized_();
    // Start at rest pose
    m_poseDirty = true;
}

Skeleton::Skeleton(const Skeleton& other)
{
    // Copy semantics:
    // - If other is base: deep copy base data (rare).
    // - If other is instance: new instance referencing same base, copying playback state.
    if (other.IsBaseSkeleton()) {
        m_isBaseSkeleton = true;
        m_boneNames = other.m_boneNames;
        m_parentIndices = other.m_parentIndices;
        m_restLocalMatrices = other.m_restLocalMatrices;
        m_evalOrder = other.m_evalOrder;
        m_inverseBindMatrices = other.m_inverseBindMatrices;

        animations = other.animations;
        animationsByName = other.animationsByName;

        m_poseDirty = true;
        return;
    }

    m_isBaseSkeleton = false;
    m_baseSkeleton = other.GetBaseSkeletonShared();
    m_animationSpeed = other.m_animationSpeed;
    m_activeAnimationIndex = other.m_activeAnimationIndex;

    EnsureInstanceBuffersSized_();

    // Copy controller state where it makes sense
    m_controllers = other.m_controllers;
    m_boneMatrices = other.m_boneMatrices;
    m_poseDirty = true;
}

std::shared_ptr<Skeleton> Skeleton::CopySkeleton(bool retainIsBaseSkeleton)
{
    if (retainIsBaseSkeleton && IsBaseSkeleton()) {
        // Deep copy base skeleton
        return std::make_shared<Skeleton>(*this);
    }

    // Default: return a runtime instance referencing this base (or this instance's base).
    if (IsBaseSkeleton()) {
        return std::make_shared<Skeleton>(shared_from_this());
    }
    return std::make_shared<Skeleton>(GetBaseSkeletonShared());
}

std::shared_ptr<Skeleton> Skeleton::GetBaseSkeletonShared() const
{
    if (m_isBaseSkeleton) {
        return std::const_pointer_cast<Skeleton>(shared_from_this());
    }
    if (!m_baseSkeleton) {
        // defensive
        spdlog::warn("Skeleton(instance): missing base skeleton pointer");
        return nullptr;
    }
    return m_baseSkeleton;
}

void Skeleton::BuildBaseFromNodes_(const std::vector<flecs::entity>& nodes)
{
    const size_t n = nodes.size();

    m_boneNames.resize(n);
    m_parentIndices.assign(n, -1);
    m_restLocalMatrices.resize(n);
    m_evalOrder.clear();

    // Map flecs entity id -> bone index
    std::unordered_map<uint64_t, uint32_t> idToIndex;
    idToIndex.reserve(n);

    for (uint32_t i = 0; i < (uint32_t)n; ++i) {
        idToIndex[nodes[i].id()] = i;
    }

    // Extract name, parent index, and rest local matrix
    for (uint32_t i = 0; i < (uint32_t)n; ++i) {
        const flecs::entity& e = nodes[i];

        // Name for animation lookup
        if (e.has<Components::AnimationName>()) {
            m_boneNames[i] = e.get<Components::AnimationName>().name;
        }
        else if (e.name() && e.name()[0] != '\0') {
            m_boneNames[i] = e.name();
        }
        else {
            m_boneNames[i] = "bone_" + std::to_string(i);
        }

        // Parent
        flecs::entity p = e.parent();
        if (p.is_valid()) {
            auto it = idToIndex.find(p.id());
            if (it != idToIndex.end()) {
                m_parentIndices[i] = (int32_t)it->second;
            }
        }

        // Rest local TRS (fallback to identity if missing)
        Components::Position pos{};
        Components::Rotation rot{};
        Components::Scale    sca{};

        if (e.has<Components::Position>()) pos = e.get<Components::Position>();
        if (e.has<Components::Rotation>()) rot = e.get<Components::Rotation>();
        if (e.has<Components::Scale>())    sca = e.get<Components::Scale>();

        m_restLocalMatrices[i] = ComposeTRS_(pos, rot, sca);
    }

    BuildEvalOrder_();
}

void Skeleton::BuildEvalOrder_()
{
    const uint32_t n = (uint32_t)m_parentIndices.size();
    m_evalOrder.clear();
    m_evalOrder.reserve(n);

    // Build children adjacency
    std::vector<std::vector<uint32_t>> children;
    children.resize(n);

    std::vector<uint32_t> roots;
    roots.reserve(n);

    for (uint32_t i = 0; i < n; ++i) {
        int32_t p = m_parentIndices[i];
        if (p < 0) roots.push_back(i);
        else children[(uint32_t)p].push_back(i);
    }

    // BFS (parents before children)
    std::queue<uint32_t> q;
    for (uint32_t r : roots) q.push(r);

    while (!q.empty()) {
        uint32_t cur = q.front();
        q.pop();
        m_evalOrder.push_back(cur);
        for (uint32_t c : children[cur]) q.push(c);
    }

    if (m_evalOrder.size() != n) {
        spdlog::warn("Skeleton: evalOrder size mismatch ({} vs {}) - possible cycle or orphaned bone",
            m_evalOrder.size(), n);
        // Fallback: ensure all bones appear at least once
        std::vector<uint8_t> seen(n, 0);
        for (auto idx : m_evalOrder) if (idx < n) seen[idx] = 1;
        for (uint32_t i = 0; i < n; ++i) if (!seen[i]) m_evalOrder.push_back(i);
    }
}

void Skeleton::AddAnimation(const std::shared_ptr<Animation>& animation)
{
    auto base = GetBaseSkeletonShared();
    if (!base) return;

    if (!base->IsBaseSkeleton()) {
        spdlog::error("Skeleton::AddAnimation: base resolution failed");
        return;
    }

    if (base->animationsByName.find(animation->name) != base->animationsByName.end()) {
        spdlog::error("Duplicate animation names are not allowed in a single skeleton: {}", animation->name);
        return;
    }

    base->animations.push_back(animation);
    base->animationsByName[animation->name] = animation;
}

void Skeleton::DeleteAllAnimations()
{
    auto base = GetBaseSkeletonShared();
    if (!base) return;

    base->animations.clear();
    base->animationsByName.clear();
}

uint32_t Skeleton::GetBoneCount() const noexcept
{
    if (m_isBaseSkeleton) return (uint32_t)m_parentIndices.size();
    auto base = m_baseSkeleton;
    return base ? (uint32_t)base->m_parentIndices.size() : 0u;
}

void Skeleton::EnsureInstanceBuffersSized_()
{
    if (m_isBaseSkeleton) return;

    auto base = GetBaseSkeletonShared();
    if (!base || !base->IsBaseSkeleton()) {
        spdlog::error("Skeleton(instance): invalid base skeleton");
        return;
    }

    const uint32_t boneCount = base->GetBoneCount();
    m_controllers.resize(boneCount);
    m_boneMatrices.resize(boneCount, DirectX::XMMatrixIdentity());

    // Match speed setting
    for (auto& c : m_controllers) {
        c.SetAnimationSpeed(m_animationSpeed);
    }
}

std::span<const Skeleton::Matrix> Skeleton::GetInverseBindMatrices() const
{
    if (m_isBaseSkeleton) {
        return m_inverseBindMatrices;
    }
    if (m_baseSkeleton) {
        return m_baseSkeleton->m_inverseBindMatrices;
    }
    return {};
}

std::span<const std::string> Skeleton::GetBoneNames() const
{
    if (m_isBaseSkeleton) return m_boneNames;
    if (m_baseSkeleton) return m_baseSkeleton->m_boneNames;
    return {};
}

std::span<const int32_t> Skeleton::GetParentIndices() const
{
    if (m_isBaseSkeleton) return m_parentIndices;
    if (m_baseSkeleton) return m_baseSkeleton->m_parentIndices;
    return {};
}

void Skeleton::SetAnimation(size_t index)
{
    if (m_isBaseSkeleton) {
        spdlog::warn("Skeleton::SetAnimation called on base skeleton - ignored");
        return;
    }

    auto base = GetBaseSkeletonShared();
    if (!base || !base->IsBaseSkeleton()) return;

    if (base->animations.size() <= index) {
        spdlog::error("Skeleton::SetAnimation index out of range");
        return;
    }

    EnsureInstanceBuffersSized_();

    auto& anim = base->animations[index];

    // Bind clips by bone name
    const uint32_t boneCount = base->GetBoneCount();
    for (uint32_t i = 0; i < boneCount; ++i) {
        auto& ctrl = m_controllers[i];
        ctrl.reset();
        ctrl.SetAnimationSpeed(m_animationSpeed);

        auto it = anim->nodesMap.find(base->m_boneNames[i]);
        if (it != anim->nodesMap.end()) {
            ctrl.setAnimationClip(it->second);
        }
        else {
            ctrl.setAnimationClip(nullptr); // fall back to rest pose
        }
    }

    m_activeAnimationIndex = index;
    m_poseDirty = true;
}

void Skeleton::SetAnimationSpeed(float speed)
{
    m_animationSpeed = speed;

    if (m_isBaseSkeleton) {
        // Base skeleton doesn't run controllers
		spdlog::warn("Skeleton::SetAnimationSpeed called on base skeleton - ignored");
        return;
    }

    for (auto& c : m_controllers) {
        c.SetAnimationSpeed(speed);
    }
    m_poseDirty = true;
}

void Skeleton::UpdateTransforms(float elapsedSeconds, bool force)
{
    if (m_isBaseSkeleton) {
        spdlog::warn("Skeleton::UpdateTransforms called on base skeleton - ignored");
        return;
    }

    auto base = GetBaseSkeletonShared();
    if (!base || !base->IsBaseSkeleton()) return;

    EnsureInstanceBuffersSized_();

    const uint32_t boneCount = base->GetBoneCount();
    if (boneCount == 0) return;

    // Evaluate local -> global in parent-before-children order
    // local = anim local TRS if clip exists else rest local
    // global = local * parentGlobal
    for (uint32_t idx : base->m_evalOrder) {
        if (idx >= boneCount) continue;

        Matrix local = base->m_restLocalMatrices[idx];

        // If a clip is bound, use animated local TRS
        auto& ctrl = m_controllers[idx];
        if (ctrl.animationClip) {
            const auto& trs = ctrl.GetUpdatedTransform(elapsedSeconds, force);
            local = ComposeTRS_(trs);
        }

        const int32_t p = base->m_parentIndices[idx];
        if (p < 0) {
            m_boneMatrices[idx] = local;
        }
        else {
            m_boneMatrices[idx] = DirectX::XMMatrixMultiply(local, m_boneMatrices[(uint32_t)p]);
        }
    }

    m_poseDirty = true;
}
