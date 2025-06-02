#pragma once

#include <type_traits>

#include "RenderGraph.h"
#include "ResourceRequirements.h"
#include "Resources/ResourceStateTracker.h"
#include "Resources/ResourceIdentifier.h"

class RenderGraphBuilder;

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

// shared_ptr
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


// ResourceIdentifier
inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r) {
    // everything
    return { r };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    Mip m)
{
    RangeSpec spec;
    spec.mipLower   = { BoundType::Exact, m.first      };
    spec.mipUpper   = { BoundType::Exact, m.first + m.count - 1 };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    FromMip fm)
{
    RangeSpec spec;
    spec.mipLower   = { BoundType::From, fm.first };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    UpToMip um)
{
    RangeSpec spec;
    spec.mipUpper   = { BoundType::UpTo, um.last };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    Slice s)
{
    RangeSpec spec;
    spec.sliceLower = { BoundType::Exact, s.first       };
    spec.sliceUpper = { BoundType::Exact, s.first + s.count - 1 };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    FromSlice fs)
{
    RangeSpec spec;
    spec.sliceLower = { BoundType::From, fs.first };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    UpToSlice us)
{
    RangeSpec spec;
    spec.sliceUpper = { BoundType::UpTo, us.last };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
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

// BuiltinResource
inline ResourceIdentifierAndRange Subresources(const BuiltinResource& r) {
	return Subresources(ResourceIdentifier{ r });
}

inline ResourceIdentifierAndRange Subresources(const BuiltinResource& r,
    Mip m) {
	return Subresources(ResourceIdentifier{ r }, m);
}

inline ResourceIdentifierAndRange Subresources(const BuiltinResource& r,
    FromMip fm) {
	return Subresources(ResourceIdentifier{ r }, fm);
}

inline ResourceIdentifierAndRange Subresources(const BuiltinResource& r,
    UpToMip um) {
	return Subresources(ResourceIdentifier{ r }, um);
}

inline ResourceIdentifierAndRange Subresources(const BuiltinResource& r,
    Slice s) {
	return Subresources(ResourceIdentifier{ r }, s);
}

inline ResourceIdentifierAndRange Subresources(const BuiltinResource& r,
    FromSlice fs) {
	return Subresources(ResourceIdentifier{ r }, fs);
}

inline ResourceIdentifierAndRange Subresources(const BuiltinResource& r,
    UpToSlice us) {
	return Subresources(ResourceIdentifier{ r }, us);
}

inline ResourceIdentifierAndRange Subresources(const BuiltinResource& r,
    Mip     m,
    Slice   s) {
	return Subresources(ResourceIdentifier{ r }, m, s);
}

//
//  expandToRanges(...) is a set of overloads that take one of six
//  input types and return a std::vector<ResourceAndRange> so that we can
//  unify all the add*() calls into a single template.
//

// If we already have a ResourceAndRange, just return it in a vector:
inline std::vector<ResourceAndRange>
expandToRanges(ResourceAndRange const & rar)
{
    if (!rar.resource) return {};
    return { rar };
}

// If we have a list of ResourceAndRange, return a vector copy of it:
inline std::vector<ResourceAndRange>
expandToRanges(std::initializer_list<ResourceAndRange> list)
{
    std::vector<ResourceAndRange> out;
    out.reserve(list.size());
    for (auto const & r : list) {
        if (!r.resource) continue;
        out.push_back(r);
    }
    return out;
}

// If we have a shared_ptr<Resource>, wrap it in a ResourceAndRange:
inline std::vector<ResourceAndRange>
expandToRanges(std::shared_ptr<Resource> const & r)
{
    if (!r) return {};
    return { ResourceAndRange{ r } };
}

// If we have an initializer_list of shared_ptr<Resource>:
inline std::vector<ResourceAndRange>
expandToRanges(std::initializer_list<std::shared_ptr<Resource>> list)
{
    std::vector<ResourceAndRange> out;
    out.reserve(list.size());
    for (auto const & r : list) {
        if (!r) continue;
        out.push_back(ResourceAndRange{ r });
    }
    return out;
}

// If we have a ResourceIdentifierAndRange, ask the builder to resolve it into an actual ResourceAndRange:
std::vector<ResourceAndRange> expandToRanges(ResourceIdentifierAndRange const& rir, RenderGraphBuilder* builder);

// If we have an initializer_list of ResourceIdentifierAndRange,
inline std::vector<ResourceAndRange>
expandToRanges(std::initializer_list<ResourceIdentifierAndRange> list,
    RenderGraphBuilder* builder)
{
    std::vector<ResourceAndRange> out;
    out.reserve(list.size());
    for (auto const & rir : list) {
        if (auto vec = expandToRanges(rir, builder); !vec.empty()) {
            // vec always has exactly one element, but we push it.
            out.push_back(std::move(vec.front()));
        }
    }
    return out;
}

inline std::vector<ResourceAndRange>
processResourceArguments(std::initializer_list<ResourceIdentifier> list, RenderGraphBuilder* builderPtr)
{
    std::vector<ResourceAndRange> out;
    out.reserve(list.size());
    for (auto const & rid : list) {
        ResourceIdentifierAndRange rir{ rid };
        if (auto vec = expandToRanges(rir, builderPtr); !vec.empty()) {
            out.push_back(std::move(vec.front()));
        }
    }
    return out;
}

inline std::vector<ResourceAndRange>
processResourceArguments(std::initializer_list<BuiltinResource> list, RenderGraphBuilder* builderPtr)
{
    std::vector<ResourceAndRange> out;
    out.reserve(list.size());
    for (auto const & bir : list) {
		ResourceIdentifier rid{ bir };
        ResourceIdentifierAndRange rir{ rid };
        if (auto vec = expandToRanges(rir, builderPtr); !vec.empty()) {
            out.push_back(std::move(vec.front()));
        }
    }
    return out;
}

template<typename T>
inline std::vector<ResourceAndRange> processResourceArguments(T&& x, RenderGraphBuilder* builderPtr) {
    if constexpr(std::is_same_v<std::decay_t<T>, ResourceAndRange> ||
        std::is_same_v<std::decay_t<T>, std::shared_ptr<Resource>> ||
        std::is_same_v<std::decay_t<T>, std::initializer_list<ResourceAndRange>> ||
        std::is_same_v<std::decay_t<T>, std::initializer_list<std::shared_ptr<Resource>>>)
    {
		std::vector<ResourceAndRange> out;
        for (auto const& rar : expandToRanges(std::forward<T>(x))) {
            if (!rar.resource) continue;
            out.push_back(rar);
        }
		return out;
    }
    else if constexpr(std::is_same_v<std::decay_t<T>, ResourceIdentifierAndRange>)
    {
        std::vector<ResourceAndRange> out;
        for (auto const& rar : expandToRanges(std::forward<T>(x), *builderPtr)) {
            if (!rar.resource) continue;
            out.push_back(rar);
        }
        return out;
    }
    else if constexpr(std::is_same_v<std::decay_t<T>, std::initializer_list<ResourceIdentifierAndRange>>)
    {
        std::vector<ResourceAndRange> out;
        for (auto const& rar : expandToRanges(std::forward<T>(x), *builderPtr)) {
            if (!rar.resource) continue;
            out.push_back(rar);
        }
        return out;
    }
    else if constexpr (std::is_same_v<std::decay_t<T>, ResourceIdentifier> ||
        std::is_same_v<std::decay_t<T>, std::initializer_list<ResourceIdentifier>>)
    {
        return processResourceArguments(std::forward<T>(x), builderPtr);
    }
    else if constexpr (std::is_same_v<std::decay_t<T>, BuiltinResource> ||
        std::is_same_v<std::decay_t<T>, std::initializer_list<BuiltinResource>>)
    {
        return processResourceArguments(std::forward<T>(x), builderPtr);
    }
    else {
        static_assert(
            sizeof(T) == 0,
            "add*(...) does not accept this argument type"
            );
    }
    return {};
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
        graph->AddRenderPass(pass, params, passName);

        return *this;
    }

    // Second build, callable on temporaries
    template<typename PassT, typename... CtorArgs>
    RenderPassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

		params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph->AddRenderPass(pass, params, passName);

        return std::move(*this);
    }

private:
    RenderPassBuilder(RenderGraph* g, RenderGraphBuilder* builder, std::string name)
        : graph(g), graphBuilder(builder), passName(std::move(name)) {}

    // Shader Resource
	template<typename T>
	RenderPassBuilder& addShaderResource(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.shaderResources.push_back(r);
		}
		return *this;
	}

    // Render target
    template<typename T>
    RenderPassBuilder& addRenderTarget(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.renderTargets.push_back(r);
		}
		return *this;
    }

    // Depth target
	template<typename T>
	RenderPassBuilder& addDepthReadWrite(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.depthReadWriteResources.push_back(r);
		}
		return *this;
	}

	template<typename T>
	RenderPassBuilder& addDepthRead(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.depthReadResources.push_back(r);
		}
		return *this;
	}

    // Constant buffer
	template<typename T>
	RenderPassBuilder& addConstantBuffer(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.constantBuffers.push_back(r);
		}
		return *this;
	}

    // Unordered access
	template<typename T>
	RenderPassBuilder& addUnorderedAccess(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.unorderedAccessViews.push_back(r);
		}
		return *this;
	}

    // Copy destination
	template<typename T>
	RenderPassBuilder& addCopyDest(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.copyTargets.push_back(r);
		}
		return *this;
	}

    // Copy source
	template<typename T>
	RenderPassBuilder& addCopySource(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.copySources.push_back(r);
		}
		return *this;
	}

    // Indirect arguments
	template<typename T>
	RenderPassBuilder& addIndirectArguments(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.indirectArgumentBuffers.push_back(r);
		}
		return *this;
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
                if (seg.state.access == ResourceAccessType::COMMON && seg.state.layout == ResourceLayout::LAYOUT_COMMON) {
                    continue; // TODO: Will we ever need explicit transitions to common for declared resources?
                }
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
    RenderGraph*             graph;
	RenderGraphBuilder* graphBuilder;
    std::string              passName;
    RenderPassParameters     params;
    bool built_ = false;

    friend class RenderGraphBuilder;
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
        graph->AddComputePass(pass, params, passName);

        return *this;
    }

    // Second build, callable on temporaries
    template<typename PassT, typename... CtorArgs>
    ComputePassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

        params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph->AddComputePass(pass, params, passName);

        return std::move(*this);
    }

private:
    ComputePassBuilder(RenderGraph* g, RenderGraphBuilder* builder, std::string name)
        : graph(g), graphBuilder(builder), passName(std::move(name)) {}

    // Shader resource
	template<typename T>
	ComputePassBuilder& addShaderResource(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.shaderResources.push_back(r);
		}
		return *this;
	}

    // Constant buffer
	template<typename T>
	ComputePassBuilder& addConstantBuffer(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.constantBuffers.push_back(r);
		}
		return *this;
	}

    // Unordered access
	template<typename T>
	ComputePassBuilder& addUnorderedAccess(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.unorderedAccessViews.push_back(r);
		}
		return *this;
	}

	// Indirect arguments
	template<typename T>
	ComputePassBuilder& addIndirectArguments(T&& x) {
		auto ranges = processResourceArguments(std::forward<T>(x), graphBuilder);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.indirectArgumentBuffers.push_back(r);
		}
		return *this;
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
                if (seg.state.access == ResourceAccessType::COMMON && seg.state.layout == ResourceLayout::LAYOUT_COMMON) {
                    continue; // TODO: Will we ever need explicit transitions to common for declared resources?
                }
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
    RenderGraph*             graph;
	RenderGraphBuilder* graphBuilder;
    std::string              passName;
    ComputePassParameters     params;
    bool built_ = false;

    friend class RenderGraphBuilder;
};