#pragma once

#include "RenderGraph.h"
#include "ResourceRequirements.h"
#include "Resources/ResourceStateTracker.h"

// Tag for a contiguous mip-range [first..first+count)
struct Mip {
	Mip(uint32_t first, uint32_t count) : first(first), count(count) {}
    uint32_t first, count;
};

// Tag for a half-open "from" mip-range [first..inf)
struct FromMip {
    uint32_t first;
};

// Tag for a half-open "up to" mip-range [0..last]
struct UpToMip {
    uint32_t last;
};

// Tag for a contiguous slice-range [first..first+count)
struct Slice {
	Slice(uint32_t first, uint32_t count) : first(first), count(count) {}
    uint32_t first, count;
};

// Tag for a half-open "from" slice-range [first..inf)
struct FromSlice {
    uint32_t first;
};

// Tag for a half-open "up to" slice-range [0..last]
struct UpToSlice {
    uint32_t last;
};

inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r) {
    // everything
    return { r };
}

inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
    Mip m)
{
    RangeSpec spec;
    spec.mipLower   = { BoundType::Exact, m.first      };
    spec.mipUpper   = { BoundType::Exact, m.first + m.count - 1 };
    return { r, spec };
}

inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
    FromMip fm)
{
    RangeSpec spec;
    spec.mipLower   = { BoundType::From, fm.first };
    return { r, spec };
}

inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
    UpToMip um)
{
    RangeSpec spec;
    spec.mipUpper   = { BoundType::UpTo, um.last };
    return { r, spec };
}

inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
    Slice s)
{
    RangeSpec spec;
    spec.sliceLower = { BoundType::Exact, s.first       };
    spec.sliceUpper = { BoundType::Exact, s.first + s.count - 1 };
    return { r, spec };
}

inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
    FromSlice fs)
{
    RangeSpec spec;
    spec.sliceLower = { BoundType::From, fs.first };
    return { r, spec };
}

inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
    UpToSlice us)
{
    RangeSpec spec;
    spec.sliceUpper = { BoundType::UpTo, us.last };
    return { r, spec };
}

inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
    Mip     m,
    Slice   s)
{
    RangeSpec spec;
    spec.mipLower   = { BoundType::Exact, m.first      };
    spec.mipUpper   = { BoundType::Exact, m.first + m.count - 1 };
    spec.sliceLower = { BoundType::Exact, s.first       };
    spec.sliceUpper = { BoundType::Exact, s.first + s.count - 1 };
    return { r, spec };
}

class RenderPassBuilder {
public:
    // Variadic entry points

    //First set, callable on Lvalues
    template<typename... Args>
    RenderPassBuilder& WithShaderResource(Args&&... args) & {
        (addShaderResource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithRenderTarget(Args&&... args) & {
        (addRenderTarget(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithDepthRead(Args&&... args) & {
        (addDepthRead(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithDepthReadWrite(Args&&... args) & {
        (addDepthReadWrite(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithConstantBuffer(Args&&... args) & {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithUnorderedAccess(Args&&... args) & {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithCopyDest(Args&&... args) & {
        (addCopyDest(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithCopySource(Args&&... args) & {
        (addCopySource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithIndirectArguments(Args&&... args) & {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return *this;
    }

    // Second set, callable on temporaries
    template<typename... Args>
    RenderPassBuilder WithShaderResource(Args&&... args) && {
        (addShaderResource(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithRenderTarget(Args&&... args) && {
        (addRenderTarget(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithDepthReadWrite(Args&&... args) && {
        (addDepthReadWrite(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithDepthRead(Args&&... args) && {
        (addDepthRead(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithConstantBuffer(Args&&... args) && {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithUnorderedAccess(Args&&... args) && {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithCopyDest(Args&&... args) && {
        (addCopyDest(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithCopySource(Args&&... args) && {
        (addCopySource(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithIndirectArguments(Args&&... args) && {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

	RenderPassBuilder& IsGeometryPass()& {
		params.isGeometryPass = true;
		return *this;
	}

    RenderPassBuilder IsGeometryPass() && {
        params.isGeometryPass = true;
		return std::move(*this);
    }

    // First build, callable on Lvalues
    template<typename PassT, typename... CtorArgs>
    RenderPassBuilder& Build(CtorArgs&&... args) & {
        ensureNotBuilt();
        built_ = true;

        params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddRenderPass(pass, params, passName);

        return *this;
    }

    // Second build, callable on temporaries
    template<typename PassT, typename... CtorArgs>
    RenderPassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

		params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddRenderPass(pass, params, passName);

        return std::move(*this);
    }

private:
    RenderPassBuilder(RenderGraph& g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Single resource overload
    void addShaderResource(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.shaderResources.push_back(r);
    }

    // initializer list overload
    void addShaderResource(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.shaderResources.push_back(r);
        }
    }

    void addShaderResource(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.shaderResources.push_back(resourceAndRange);
    }

    void addShaderResource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.shaderResources.push_back(resourceAndRange);
        }
    }

    // Render target
    void addRenderTarget(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.renderTargets.push_back(r);
    }

    void addRenderTarget(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.renderTargets.push_back(r);
        }
    }

    void addRenderTarget(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.renderTargets.push_back(resourceAndRange);
    }

    void addRenderTarget(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.renderTargets.push_back(resourceAndRange);
        }
    }

    // Depth target
    void addDepthReadWrite(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.depthReadWriteResources.push_back(r);
    }

    void addDepthReadWrite(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.depthReadWriteResources.push_back(r);
        }
    }

    void addDepthReadWrite(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.depthReadWriteResources.push_back(resourceAndRange);
    }

    void addDepthReadWrite(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.depthReadWriteResources.push_back(resourceAndRange);
        }
    }

    void addDepthRead(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.depthReadResources.push_back(r);
    }

    void addDepthRead(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.depthReadResources.push_back(r);
        }
    }

    void addDepthRead(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.depthReadResources.push_back(resourceAndRange);
    }

    void addDepthRead(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.depthReadResources.push_back(resourceAndRange);
        }
    }

    // Constant buffer
    void addConstantBuffer(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.constantBuffers.push_back(r);
    }

    void addConstantBuffer(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.constantBuffers.push_back(r);
        }
    }

    void addConstantBuffer(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.constantBuffers.push_back(resourceAndRange);
    }

    void addConstantBuffer(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.constantBuffers.push_back(resourceAndRange);
        }
    }

    // Unordered access
    void addUnorderedAccess(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.unorderedAccessViews.push_back(r);
    }

    void addUnorderedAccess(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.unorderedAccessViews.push_back(r);
        }
    }

    void addUnorderedAccess(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.unorderedAccessViews.push_back(resourceAndRange);
    }

    void addUnorderedAccess(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.unorderedAccessViews.push_back(resourceAndRange);
        }
    }

    // Copy destination
    void addCopyDest(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.copyTargets.push_back(r);
    }

    void addCopyDest(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.copyTargets.push_back(r);
        }
    }

    void addCopyDest(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.copyTargets.push_back(resourceAndRange);
    }

    void addCopyDest(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.copyTargets.push_back(resourceAndRange);
        }
    }

    // Copy source
    void addCopySource(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.copySources.push_back(r);
    }

    void addCopySource(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.copySources.push_back(r);
        }
    }

    void addCopySource(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.copySources.push_back(resourceAndRange);
    }

    void addCopySource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.copySources.push_back(resourceAndRange);
        }
    }

    // Indirect arguments
    void addIndirectArguments(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.indirectArgumentBuffers.push_back(r);
    }

    void addIndirectArguments(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.indirectArgumentBuffers.push_back(r);
        }
    }

    void addIndirectArguments(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.indirectArgumentBuffers.push_back(resourceAndRange);
    }

    void addIndirectArguments(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.indirectArgumentBuffers.push_back(resourceAndRange);
        }
    }

    void ensureNotBuilt() const {
        if (built_) throw std::runtime_error("RenderPassBuilder::Build() may only be called once");
    }


    std::vector<ResourceRequirement> GatherResourceRequirements() const {
        // Collect every (ResourceAndRange,AccessFlag) pair from all the With* lists
        std::vector<std::pair<ResourceAndRange,ResourceAccessType>> entries;
        entries.reserve(
            params.shaderResources.size()
            + params.constantBuffers.size()
            + params.renderTargets.size()
            + params.depthReadResources.size()
            + params.depthReadWriteResources.size()
            + params.unorderedAccessViews.size()
            + params.copySources.size()
            + params.copyTargets.size()
            + params.indirectArgumentBuffers.size()
        );

        auto accumulate = [&](auto const& list, ResourceAccessType flag){
            for(auto const& rr : list){
                if(!rr.resource) continue;
                entries.emplace_back(rr, flag);
            }
            };

        accumulate(params.shaderResources,         ResourceAccessType::SHADER_RESOURCE);
        accumulate(params.constantBuffers,         ResourceAccessType::CONSTANT_BUFFER);
        accumulate(params.renderTargets,           ResourceAccessType::RENDER_TARGET);
        accumulate(params.depthReadResources,      ResourceAccessType::DEPTH_READ);
        accumulate(params.depthReadWriteResources, ResourceAccessType::DEPTH_READ_WRITE);
        accumulate(params.unorderedAccessViews,    ResourceAccessType::UNORDERED_ACCESS);
        accumulate(params.copySources,             ResourceAccessType::COPY_SOURCE);
        accumulate(params.copyTargets,             ResourceAccessType::COPY_DEST);
        accumulate(params.indirectArgumentBuffers, ResourceAccessType::INDIRECT_ARGUMENT);

        // Build a tracker for each resource, applying each (range->state)
        constexpr ResourceState initialState{
            ResourceAccessType::COMMON,
            ResourceLayout   ::LAYOUT_COMMON,
            ResourceSyncState::ALL
        };

        std::unordered_map<uint64_t,SymbolicTracker> trackers;
        std::unordered_map<uint64_t,std::shared_ptr<Resource>> ptrMap;

        for(auto& [rar, flag] : entries) {
            uint64_t id = rar.resource->GetGlobalResourceID();
            ptrMap[id] = rar.resource;

            // Create a tracker spanning "all" with initial NONE state
            auto [it, inserted] = trackers.try_emplace(
                id,
                /*whole=*/ RangeSpec{},
                /*init=*/  initialState
            );
            auto& tracker = it->second;

            // Find the desired state for this entry
            ResourceState want {
                flag,
                AccessToLayout(flag, /*isRender=*/true),
                RenderSyncFromAccess(flag)
            };

            // Apply- use a dummy vector since we don't need per-pass transitions here
            std::vector<ResourceTransition> dummy;
            tracker.Apply(rar.range, rar.resource.get(), want, dummy);
        }

        // Flatten each tracker’s segments into a ResourceRequirement
        std::vector<ResourceRequirement> out;
        out.reserve(trackers.size());  // rough

        for(auto& [id, tracker] : trackers) {
            auto pRes = ptrMap[id];
            for(auto const& seg : tracker.GetSegments()) {
                // build a ResourceAndRange for this segment
                ResourceAndRange rr(pRes);
                rr.range = seg.rangeSpec;

                ResourceRequirement req(rr);
                req.state = seg.state;
                out.push_back(std::move(req));
            }
        }

        return out;
    }

    // storage
    RenderGraph&             graph;
    std::string              passName;
    RenderPassParameters     params;
    bool built_ = false;

    friend class RenderGraph;
};

class ComputePassBuilder {
public:
    // Variadic entry points

    //First set, callable on Lvalues
    template<typename... Args>
    ComputePassBuilder& WithShaderResource(Args&&... args) & {
        (addShaderResource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    ComputePassBuilder& WithConstantBuffer(Args&&... args) & {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    ComputePassBuilder& WithUnorderedAccess(Args&&... args) & {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    ComputePassBuilder& WithIndirectArguments(Args&&... args) & {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return *this;
    }

    // Second set, callable on temporaries
    template<typename... Args>
    ComputePassBuilder WithShaderResource(Args&&... args) && {
        (addShaderResource(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    ComputePassBuilder WithConstantBuffer(Args&&... args) && {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    ComputePassBuilder WithUnorderedAccess(Args&&... args) && {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    ComputePassBuilder WithIndirectArguments(Args&&... args) && {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    // First build, callable on Lvalues
    template<typename PassT, typename... CtorArgs>
    ComputePassBuilder& Build(CtorArgs&&... args) & {
        ensureNotBuilt();
        built_ = true;

        params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddComputePass(pass, params, passName);

        return *this;
    }

    // Second build, callable on temporaries
    template<typename PassT, typename... CtorArgs>
    ComputePassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

        params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddComputePass(pass, params, passName);

        return std::move(*this);
    }

private:
    ComputePassBuilder(RenderGraph& g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Single resource overload
    void addShaderResource(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.shaderResources.push_back(r);
    }

    // initializer list overload
    void addShaderResource(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.shaderResources.push_back(r);
        }
    }

    void addShaderResource(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.shaderResources.push_back(resourceAndRange);
    }

    void addShaderResource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.shaderResources.push_back(resourceAndRange);
        }
    }

    // Constant buffer
    void addConstantBuffer(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.constantBuffers.push_back(r);
    }

    void addConstantBuffer(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.constantBuffers.push_back(r);
        }
    }

    void addConstantBuffer(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.constantBuffers.push_back(resourceAndRange);
    }

    void addConstantBuffer(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.constantBuffers.push_back(resourceAndRange);
        }
    }

    // Unordered access
    void addUnorderedAccess(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.unorderedAccessViews.push_back(r);
    }

    void addUnorderedAccess(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.unorderedAccessViews.push_back(r);
        }
    }

    void addUnorderedAccess(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.unorderedAccessViews.push_back(resourceAndRange);
    }

    void addUnorderedAccess(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.unorderedAccessViews.push_back(resourceAndRange);
        }
    }

	// Indirect arguments
	void addIndirectArguments(const ResourceAndRange& r) {
		if (!r.resource) return;
		params.indirectArgumentBuffers.push_back(r);
	}
	void addIndirectArguments(std::initializer_list<ResourceAndRange> list) {
		for (auto& r : list) {
			if (!r.resource) continue;
			params.indirectArgumentBuffers.push_back(r);
		}
	}

    void addIndirectArguments(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.indirectArgumentBuffers.push_back(resourceAndRange);
    }

    void addIndirectArguments(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.indirectArgumentBuffers.push_back(resourceAndRange);
        }
    }

    void ensureNotBuilt() const {
        if (built_) throw std::runtime_error("ComputePassBuilder::Build() may only be called once");
    }

    std::vector<ResourceRequirement> GatherResourceRequirements() const {
        // Collect every (ResourceAndRange,AccessFlag) pair from all the With* lists
        std::vector<std::pair<ResourceAndRange,ResourceAccessType>> entries;
        entries.reserve(
            params.shaderResources.size()
            + params.constantBuffers.size()
            + params.unorderedAccessViews.size()
            + params.indirectArgumentBuffers.size()
        );

        auto accumulate = [&](auto const& list, ResourceAccessType flag){
            for(auto const& rr : list){
                if(!rr.resource) continue;
                entries.emplace_back(rr, flag);
            }
            };

        accumulate(params.shaderResources,         ResourceAccessType::SHADER_RESOURCE);
        accumulate(params.constantBuffers,         ResourceAccessType::CONSTANT_BUFFER);
        accumulate(params.unorderedAccessViews,    ResourceAccessType::UNORDERED_ACCESS);
        accumulate(params.indirectArgumentBuffers, ResourceAccessType::INDIRECT_ARGUMENT);

        // Build a tracker for each resource, applying each (range->state)
        constexpr ResourceState initialState{
            ResourceAccessType::COMMON,
            ResourceLayout   ::LAYOUT_COMMON,
            ResourceSyncState::ALL
        };

        std::unordered_map<uint64_t,SymbolicTracker> trackers;
        std::unordered_map<uint64_t,std::shared_ptr<Resource>> ptrMap;

        for(auto& [rar, flag] : entries) {
            uint64_t id = rar.resource->GetGlobalResourceID();
            ptrMap[id] = rar.resource;

            // Create a tracker spanning "all" with initial NONE state
            auto [it, inserted] = trackers.try_emplace(
                id,
                /*whole=*/ RangeSpec{},
                /*init=*/  initialState
            );
            auto& tracker = it->second;

            // Find the desired state for this entry
            ResourceState want {
                flag,
                AccessToLayout(flag, /*isRender=*/true),
                ComputeSyncFromAccess(flag)
            };

            // Apply- use a dummy vector since we don't need per-pass transitions here
            std::vector<ResourceTransition> dummy;
            tracker.Apply(rar.range, rar.resource.get(), want, dummy);
        }

        // Flatten each tracker’s segments into a ResourceRequirement
        std::vector<ResourceRequirement> out;
        out.reserve(trackers.size());  // rough

        for(auto& [id, tracker] : trackers) {
            auto pRes = ptrMap[id];
            for(auto const& seg : tracker.GetSegments()) {
                // build a ResourceAndRange for this segment
                ResourceAndRange rr(pRes);
                rr.range = seg.rangeSpec;

                ResourceRequirement req(rr);
                req.state = seg.state;
                out.push_back(std::move(req));
            }
        }

        return out;
    }

    // storage
    RenderGraph&             graph;
    std::string              passName;
    ComputePassParameters     params;
    bool built_ = false;

    friend class RenderGraph;
};