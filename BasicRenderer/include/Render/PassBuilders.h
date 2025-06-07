#pragma once

#include <type_traits>
#include <memory>

#include "RenderGraph.h"
#include "ResourceRequirements.h"
#include "Resources/ResourceStateTracker.h"
#include "Resources/ResourceIdentifier.h"

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
//inline std::vector<ResourceAndRange>
//expandToRanges(ResourceAndRange const & rar, RenderGraph* graph)
//{
//    if (!rar.resource) return {};
//    return { rar };
//}
//
//// If we have a list of ResourceAndRange, return a vector copy of it:
//inline std::vector<ResourceAndRange>
//expandToRanges(std::initializer_list<ResourceAndRange> list, RenderGraph* graph)
//{
//    std::vector<ResourceAndRange> out;
//    out.reserve(list.size());
//    for (auto const & r : list) {
//        if (!r.resource) continue;
//        out.push_back(r);
//    }
//    return out;
//}

// If we have a shared_ptr<Resource>, wrap it in a ResourceAndRange:
//template<typename U>
//inline std::enable_if_t<std::is_base_of_v<Resource, U>,
//    std::vector<ResourceAndRange>>
//    expandToRanges(std::shared_ptr<U> const& r, RenderGraph* graph)
//{
//    if (!r) return {};
//    std::shared_ptr<Resource> basePtr = r;
//    return { ResourceAndRange{ basePtr } };
//}

// If we have an initializer_list of shared_ptr<Resource>:
//inline std::vector<ResourceAndRange>
//expandToRanges(std::initializer_list<std::shared_ptr<Resource>> list, RenderGraph* graph)
//{
//    std::vector<ResourceAndRange> out;
//    out.reserve(list.size());
//    for (auto const & r : list) {
//        if (!r) continue;
//        out.push_back(ResourceAndRange{ r });
//    }
//    return out;
//}

// If we have a ResourceIdentifierAndRange, ask the builder to resolve it into an actual ResourceAndRange:
std::vector<ResourceAndRange> expandToRanges(ResourceIdentifierAndRange const& rir, RenderGraph* graph);

// If we have an initializer_list of ResourceIdentifierAndRange,
inline std::vector<ResourceAndRange>
expandToRanges(std::initializer_list<ResourceIdentifierAndRange> list,
    RenderGraph* graph)
{
    std::vector<ResourceAndRange> out;
    out.reserve(list.size());
    for (auto const & rir : list) {
        if (auto vec = expandToRanges(rir, graph); !vec.empty()) {
            // vec always has exactly one element, but we push it.
            out.push_back(std::move(vec.front()));
        }
    }
    return out;
}

template<typename> 
constexpr bool is_shared_ptr_v = false;

template<typename U> 
constexpr bool is_shared_ptr_v<std::shared_ptr<U>> = true;

inline std::vector<ResourceAndRange>
processResourceArguments(const ResourceAndRange& rar,
    RenderGraph* graph)
{
    if (!rar.resource) return {};
    return { rar };
}

//template<typename U>
//inline std::enable_if_t<
//    std::is_base_of_v<Resource, U>,
//    std::vector<ResourceAndRange>
//>
//processResourceArguments(const std::shared_ptr<U>& childPtr,
//    RenderGraph* graph)
//{
//    if (!childPtr) return {};
//
//    std::shared_ptr<Resource> basePtr = childPtr;
//    return expandToRanges(basePtr, graph);
//}

inline std::vector<ResourceAndRange>
processResourceArguments(const ResourceIdentifierAndRange& rir,
    RenderGraph* graph)
{
    return expandToRanges(rir, graph);
}

inline std::vector<ResourceAndRange>
processResourceArguments(const ResourceIdentifier& rid,
    RenderGraph* graph)
{
    return processResourceArguments(
        ResourceIdentifierAndRange{ rid },
        graph
    );
}

inline std::vector<ResourceAndRange>
processResourceArguments(const BuiltinResource& br,
    RenderGraph* graph)
{
    return processResourceArguments(
        ResourceIdentifierAndRange{ ResourceIdentifier{ br } },
        graph
    );
}

template<typename T>
inline std::enable_if_t<
    std::is_same_v<std::decay_t<T>, std::initializer_list<typename std::decay_t<T>::value_type>>,
    std::vector<ResourceAndRange>
>
processResourceArguments(T&& list, RenderGraph* graph)
{
    std::vector<ResourceAndRange> out;
    out.reserve(list.size());

    for (auto const & elem : list) {
        auto vec = processResourceArguments(elem, graph);
        if (!vec.empty()) 
            out.push_back(std::move(vec.front()));
    }
    return out;
}

namespace detail {
    template<typename U>
    inline void extractId(std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher>& out, std::shared_ptr<U> const&) {
        // TODO
        throw std::runtime_error("extractId not implemented for shared_ptr<Resource>");
    }
    inline void extractId(auto& out, const ResourceAndRange& rar) {
		extractId(out, rar.resource); // empty identifier
    }
    inline void extractId(auto& out, ResourceIdentifierAndRange const& rir) {
        out.insert(rir.identifier);
    }
    inline void extractId(auto& out, ResourceIdentifier const& rid) {
        out.insert(rid);
    }
    inline void extractId(auto& out, BuiltinResource br) {
        out.insert(ResourceIdentifier{ br });
    }

    template<typename T>
    inline void extractId(auto& out, std::initializer_list<T> list) {
        for (auto const& e : list) extractId(out, e);
    }
}

template<typename T>
concept DerivedRenderPass = std::derived_from<T, RenderPass>;

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
    template<DerivedRenderPass PassT, typename... CtorArgs>
    RenderPassBuilder& Build(CtorArgs&&... args) & {
        ensureNotBuilt();
        built_ = true;

        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        pass->DeclareResourceUsages(this);

        params.identifierSet = _declaredIds;
        params.resourceRequirements = GatherResourceRequirements();

        graph->AddRenderPass(pass, params, passName);

        return *this;
    }

    // Second build, callable on temporaries
    template<DerivedRenderPass PassT, typename... CtorArgs>
    RenderPassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        pass->DeclareResourceUsages(this);

        params.identifierSet = _declaredIds;
        params.resourceRequirements = GatherResourceRequirements();

        graph->AddRenderPass(pass, params, passName);

        return std::move(*this);
    }

    auto const& DeclaredResourceIds() const { return _declaredIds; }

private:
    RenderPassBuilder(RenderGraph* g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Shader Resource
	template<typename T>
	RenderPassBuilder& addShaderResource(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.shaderResources.push_back(r);
		}
		return *this;
	}

    // Render target
    template<typename T>
    RenderPassBuilder& addRenderTarget(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.renderTargets.push_back(r);
		}
		return *this;
    }

    // Depth target
	template<typename T>
	RenderPassBuilder& addDepthReadWrite(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.depthReadWriteResources.push_back(r);
		}
		return *this;
	}

	template<typename T>
	RenderPassBuilder& addDepthRead(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.depthReadResources.push_back(r);
		}
		return *this;
	}

    // Constant buffer
	template<typename T>
	RenderPassBuilder& addConstantBuffer(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.constantBuffers.push_back(r);
		}
		return *this;
	}

    // Unordered access
	template<typename T>
	RenderPassBuilder& addUnorderedAccess(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.unorderedAccessViews.push_back(r);
		}
		return *this;
	}

    // Copy destination
	template<typename T>
	RenderPassBuilder& addCopyDest(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.copyTargets.push_back(r);
		}
		return *this;
	}

    // Copy source
	template<typename T>
	RenderPassBuilder& addCopySource(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.copySources.push_back(r);
		}
		return *this;
	}

    // Indirect arguments
	template<typename T>
	RenderPassBuilder& addIndirectArguments(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
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
    std::string              passName;
    RenderPassParameters     params;
    bool built_ = false;
    std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher> _declaredIds;

    friend class RenderGraph; // Allow RenderGraph to create instances of this builder
};

template<typename T>
concept DerivedComputePass = std::derived_from<T, ComputePass>;

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
    template<DerivedComputePass PassT, typename... CtorArgs>
    ComputePassBuilder& Build(CtorArgs&&... args) & {
        ensureNotBuilt();
        built_ = true;

        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        pass->DeclareResourceUsages(this);

        params.identifierSet = _declaredIds;
        params.resourceRequirements = GatherResourceRequirements();

        graph->AddComputePass(pass, params, passName);

        return *this;
    }

    // Second build, callable on temporaries
    template<DerivedComputePass PassT, typename... CtorArgs>
    ComputePassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        pass->DeclareResourceUsages(this);

		params.identifierSet = _declaredIds;
        params.resourceRequirements = GatherResourceRequirements();

        graph->AddComputePass(pass, params, passName);

        return std::move(*this);
    }

    auto const& DeclaredResourceIds() const { return _declaredIds; }

private:
    ComputePassBuilder(RenderGraph* g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Shader resource
	template<typename T>
	ComputePassBuilder& addShaderResource(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.shaderResources.push_back(r);
		}
		return *this;
	}

    // Constant buffer
	template<typename T>
	ComputePassBuilder& addConstantBuffer(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.constantBuffers.push_back(r);
		}
		return *this;
	}

    // Unordered access
	template<typename T>
	ComputePassBuilder& addUnorderedAccess(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			if (!r.resource) continue;
			params.unorderedAccessViews.push_back(r);
		}
		return *this;
	}

	// Indirect arguments
	template<typename T>
	ComputePassBuilder& addIndirectArguments(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
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
    std::string              passName;
    ComputePassParameters     params;
    bool built_ = false;
    std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher> _declaredIds;

	friend class RenderGraph; // Allow RenderGraph to create instances of this builder
};